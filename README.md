Purple Cron
================================

A plugion for Pidgin IM and Finch to periodically run scripts and reply to conversations

# Install
See INSTALL

# Setting up cron scripts

Add scripts to `~/.purple/purplecron.d`. Scripts must be executable (chmod +x).
Every script must start with a number 00..99. For example `01_report_bithdays.sh`. This allows you to configure some sort of priority of the messages.
Modify the `~/.purple/purplecron` to adjust for your needs.

Scripts must return a JSON object about the message to be sent. Empty messages will be skipped. 
Every message JSON object must be on one line. Multiple JSON objects can be retuned - each on their own line.
See scripts/example.sh

JSON:
```
{"recipient": "3083501905061751", "message": "This is a message to be sent"}
```

*recipient* - Id of the receiver - chat id, buddy id
*message* - Message content



# TODO / Ideas

- Allow specifying a broadcast "to every conversation" messages. Maybe ?
- Add one extra callback for sync updates - send buddy lists, statuses etc so that background scripts could get some info of current state. 
    
