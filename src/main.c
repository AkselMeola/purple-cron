/*
 * 	Purple Cron
 *
 * 	Copyright (C) 2020, Aksel Meola <aksel@meola.eu>
 *
 *	Simple chat bot plugin to entertain friends. 
 *
 *	----
 *	This program is free software; you can redistribute it and/or
 * 	modify it under the terms of the GNU General Public License as
 * 	published by the Free Software Foundation; either version 2 of the
 * 	License, or (at your option) any later version.
 *
 * 	This program is distributed in the hope that it will be useful, but
 * 	WITHOUT ANY WARRANTY; without even the implied warranty of
 * 	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * 	General Public License for more details.
 *
 * 	You should have received a copy of the GNU General Public License
 * 	along with this program; if not, write to the Free Software
 * 	Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 	02111-1301, USA.
 *
 */

#define _GNU_SOURCE
//#define __WIN32__
#ifndef __WIN32__
	#define PURPLECRON_EXT ""
#else
	#define PURPLECRON_EXT ".exe"
#endif

#define PLUGIN_ID "purplecron"
#define PURPLECRON PLUGIN_ID PURPLECRON_EXT
#define PURPLECRON_CRON_INTERVAL_SECONDS_DEFAULT 60
#define PURPLECRON_JOB_TIMEOUT 250

#define PURPLECRON_LINE_LENGTH 4096
#define PROTOCOL_PREFIX "prpl-"



#define PREF_PREFIX "/plugins/" PLUGIN_ID
#define PREF_CRON_INTERVAL_SECONDS	PREF_PREFIX "/interval"


#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "jansson.h"

#ifndef __WIN32__
	#include <fcntl.h>
#else
	#include <windows.h>
#endif

/* Purple plugin */
#define PURPLE_PLUGINS
#include <libpurple/account.h>
#include <libpurple/blist.h>
#include <libpurple/conversation.h>
#include <libpurple/core.h>
#include <libpurple/debug.h>
#include <libpurple/plugin.h>
#include <libpurple/savedstatuses.h>
#include <libpurple/signals.h>
#include <libpurple/status.h>
#include <libpurple/util.h>
#include <libpurple/value.h>
#include <libpurple/version.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

/** 
 * Script acting as the entry script for all cron jobs
 */
char *entryScript = NULL;


/** 
 * We're adding this here and assigning it in plugin_load because we need
 * a valid plugin handle for our calls in various plugin functions.  
 * 
 * TODO: Perhaps there is another way ? 
 */
PurplePlugin *purpleCronPluginHandle = NULL;


/**
 * Job for cron 
 */
typedef struct {
  FILE *pipe;
  PurpleConversation *conv;
} cronJob;

/**
 * Extract data from message object
 */
char * getMessageAttribute(const char *messageData, const char * attributeName)
{
	json_t *jsonObject, *messageNode;
	json_error_t error;

	const char * jsonString;
	char * attributeValue;
	attributeValue = malloc( 255 * sizeof(char) );
	attributeValue[0] = '\0';

	jsonObject = json_loads(messageData, 0 , &error);

	if (!jsonObject) {
		return NULL;
    }

	messageNode = json_object_get(jsonObject, attributeName);

	jsonString = json_string_value(messageNode);
	// Return empty string if string cant be read (numbers should be passed also as strings)
	// TODO: Convert numbers to strings
	if (jsonString == NULL) {
		return NULL;
	}

	strcat(attributeValue, json_string_value(messageNode));
	json_decref(jsonObject);

	return attributeValue;
}

/**
 * Handle the message object received from cron
 */ 
gboolean handleMessageCallback(const char * messageData)
{
	GList * accounts;
	PurpleAccount * account;
	PurpleConversation * conv;
	int i;

	purple_debug_info("purple-cron", "Handling message: %s\n", messageData);

	accounts = purple_accounts_get_all();

	for(i = 0; i < (int)g_list_length(accounts); i++) {
		account = g_list_nth_data(accounts, i);

		if (!purple_account_is_connected(account)) {
			continue;
		}
		
		char * messageText = getMessageAttribute(messageData, "message");
		char * messageRecipient = getMessageAttribute(messageData, "recipient");
		

		if (messageText == NULL || messageRecipient == NULL) {
			fprintf(stderr, "\nMalformed message\n");
			return FALSE;
		}

		conv = purple_conversation_new(PURPLE_CONV_TYPE_IM, account, messageRecipient);
		if (!conv) {
			return FALSE;
		}

			
		if(purple_conversation_get_type(conv) == PURPLE_CONV_TYPE_CHAT) {
			purple_conv_chat_send(purple_conversation_get_chat_data(conv), messageText);
		} else {
			purple_conv_im_send(purple_conversation_get_im_data(conv), messageText);
		}	
	}

	return FALSE;
}


/**
 * Handle cron job output
 */
gboolean handleCronResults(cronJob * job)
{
 	int i;
	char messageLine[PURPLECRON_LINE_LENGTH + 1]; 
	messageLine[0] = '\0';
	
	FILE *pipe = job->pipe;

	if (pipe && !feof(pipe)) {
		// Try again
		if (!fgets(messageLine, PURPLECRON_LINE_LENGTH, pipe) && (errno == EWOULDBLOCK || errno == EAGAIN)) {
			return TRUE;
		}

		for (i = 0; messageLine[i]; i++ ) {
			if (messageLine[i] == '\n') {
				messageLine[i] = '\0';
			}
		}

		if (messageLine[0] != '\0') {
			handleMessageCallback(messageLine);
		}

		// Read again
		if (!feof(pipe)) {
			return TRUE;
		}
	}

	pclose(pipe);
	free(job);

	return FALSE;
}

/**
 * Run entry script and dispatch the output to conversation handlers. 
 */
gboolean runEntryScriptJobCallback()
{
	cronJob *job = (cronJob*) malloc(sizeof(cronJob));

	job->pipe = popen(entryScript, "r");
	
	if (job->pipe == NULL) {
		fprintf(stderr, "Can't execute %s\n", entryScript);
		return FALSE;
	}

	#ifndef __WIN32__
		int fflags = fcntl(fileno(job->pipe), F_GETFL, 0);
		fcntl(fileno(job->pipe), F_SETFL, fflags | O_NONBLOCK);
	#endif

	purple_timeout_add(PURPLECRON_JOB_TIMEOUT, (GSourceFunc) handleCronResults, (gpointer) job);

	return TRUE;
}

/**
 * Loading plugin
 * Setting up callbacks
 */
static gboolean plugin_load(PurplePlugin * plugin)
{
	// Setup the cron entry script 
	asprintf(&entryScript, "%s/%s", purple_user_dir(), PURPLECRON);
	//void *conversationHandle = purple_conversations_get_handle();

	// Store handle
	purpleCronPluginHandle = plugin;

	// NOTE: Not handling any incoming messages
	// Might take some commands in future
	// purple_signal_connect(conversationHandle, "received-im-msg", plugin, PURPLE_CALLBACK(received_msg_cb), NULL);
	// purple_signal_connect(conversationHandle, "received-chat-msg", plugin, PURPLE_CALLBACK(received_msg_cb), NULL);

	// Setup the cron to answer every 60 minutes
	purple_timeout_add_seconds(purple_prefs_get_int(PREF_CRON_INTERVAL_SECONDS), (GSourceFunc) runEntryScriptJobCallback, NULL);

	return TRUE;
}

/**
 * Unload plugin
 * Free resources and clean up 
 */
static gboolean plugin_unload(PurplePlugin * plugin)
{
	free(entryScript);
	return TRUE;
}


static PurplePluginPrefFrame *
get_plugin_pref_frame(PurplePlugin *plugin)
{
	PurplePluginPrefFrame *frame;
	PurplePluginPref *pref;

	frame = purple_plugin_pref_frame_new();

	pref = purple_plugin_pref_new_with_name_and_label(PREF_CRON_INTERVAL_SECONDS, "Cron interval (seconds)");
	purple_plugin_pref_set_bounds(pref, 5, 3600);
	purple_plugin_pref_frame_add(frame, pref);

	/*
	pref = purple_plugin_pref_new_with_name_and_label(PREF_STRANGER,
					_("When a file-transfer request arrives from a user who is\n"
                      "*not* on your buddy list:"));
	purple_plugin_pref_set_type(pref, PURPLE_PLUGIN_PREF_CHOICE);
	purple_plugin_pref_add_choice(pref, _("Ask"), GINT_TO_POINTER(FT_ASK));
	purple_plugin_pref_add_choice(pref, _("Auto Accept"), GINT_TO_POINTER(FT_ACCEPT));
	purple_plugin_pref_add_choice(pref, _("Auto Reject"), GINT_TO_POINTER(FT_REJECT));
	purple_plugin_pref_frame_add(frame, pref);

	pref = purple_plugin_pref_new_with_name_and_label(PREF_NOTIFY,
					_("Notify with a popup when an autoaccepted file transfer is complete\n"
					  "(only when there's no conversation with the sender)"));
	purple_plugin_pref_frame_add(frame, pref);

	pref = purple_plugin_pref_new_with_name_and_label(PREF_NEWDIR,
			_("Create a new directory for each user"));
	purple_plugin_pref_frame_add(frame, pref);

	pref = purple_plugin_pref_new_with_name_and_label(PREF_ESCAPE,
			_("Escape the filenames"));
	purple_plugin_pref_frame_add(frame, pref);
	*/
	return frame;
}

static PurplePluginUiInfo prefs_info = {
	get_plugin_pref_frame,
	0,
	NULL,

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};


/**
 * Plugin meta
 */
static PurplePluginInfo pluginInfo = {
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,
	NULL,
	0,
	NULL,
	PURPLE_PRIORITY_DEFAULT,

	"purplecron",
	"Purple Cron",
	"0.1",
	"Run a script periodically to answer conversations",
	// TODO: Write a description
	"\n Write a description",
	"Aksel Meola <aksel@meola.eu>",
	"https://github.com/AkselMeola/purple-cron",
	plugin_load,
	plugin_unload,
	NULL,
	NULL,
	NULL,
	&prefs_info,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};

/**
 * Initialize plugin 
 */
static void init_plugin(PurplePlugin * plugin)
{
	char *dirname;

	dirname = g_build_filename(purple_user_dir(), "purplecron", NULL);
	purple_prefs_add_none(PREF_PREFIX);
	purple_prefs_add_int(PREF_CRON_INTERVAL_SECONDS, PURPLECRON_CRON_INTERVAL_SECONDS_DEFAULT);
	g_free(dirname);
}

PURPLE_INIT_PLUGIN(purplecron, init_plugin, pluginInfo)
