if(BUILD_MONGO_DB_PLUGIN)
    file(GLOB HEADERS "include/eosio/mongo_history_plugin/*.hpp")
    add_library( mongo_history_plugin
                mongo_history_plugin.cpp
                ${HEADERS} )

    target_link_libraries( mongo_history_plugin chain_plugin eosio_chain appbase )
    target_include_directories( mongo_history_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" )

else()
  message("mongo_history_plugin not selected and will be omitted.")
endif()