# mongo_history_plugin

This plugin in conjunction with it's counterpart [mongo_history_api_plugin](https://github.com/deckb/mongo_history_api_plugin) is supposed to be a drop-in replacement for the history_plugin and history_api_plugin. 

## Install instructions

1. Pull down into your EOS plugin software
```
cd <EOS source dir>/plugins
git clone https://github.com/deckb/mongo_history_api_plugin
git clone https://github.com/deckb/mongo_history_plugin
```

2. Add the following to <EOS source dir>/plugins/CMakeLists.txt
```
# mongo history plugins
add_subdirectory(mongo_history_plugin)
add_subdirectory(mongo_history_api_plugin)
```

3. Ensure the following is in <EOS source dir>/programs/nodeos/CMakeLists.txt
```
if(BUILD_MONGO_DB_PLUGIN)
  target_link_libraries( ${NODE_EXECUTABLE_NAME} PRIVATE -Wl,${whole_archive_flag} mongo_db_plugin -Wl,${no_whole_archive_flag} )
  target_link_libraries( ${NODE_EXECUTABLE_NAME} PRIVATE -Wl,${whole_archive_flag} mongo_history_plugin -Wl,${no_whole_archive_flag} )
  target_link_libraries( ${NODE_EXECUTABLE_NAME} PRIVATE -Wl,${whole_archive_flag} mongo_history_api_plugin -Wl,${no_whole_archive_flag} )
endif()
```

4. Build eos.io as normal

## Configure 

1. 'history-mongodb-uri'
```
# uri of the mongodb server
history-mongodb-uri = mongodb://localhost:27001
```

2. Plugins
```
plugin = eosio::mongo_history_plugin
plugin = eosio::mongo_history_api_plugin
```

3. MongoDB
```
# collections used:
#   transaction_traces
#   pub_keys
#   account_controls

# set index
db.transaction_traces.createIndex({"action_traces.act.authorization.actor" : 1})
db.transaction_traces.createIndex({"action_traces.inline_traces.receipt.receiver" : 1})
db.transaction_traces.createIndex({"action_traces.receipt.receiver" : 1})
# reindex
db.transaction_traces.reIndex()
```