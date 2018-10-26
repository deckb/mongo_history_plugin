#include <eosio/mongo_history_plugin/mongo_history_plugin.hpp>
#include <eosio/mongo_history_plugin/account_control_history_object.hpp>
#include <eosio/mongo_history_plugin/public_key_history_object.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

#include <fc/io/json.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/exception/operation_exception.hpp>
#include <mongocxx/exception/logic_error.hpp>

#include <boost/algorithm/string.hpp>
#include <boost/signals2/connection.hpp>

namespace eosio {
    using namespace chain;
    using boost::signals2::scoped_connection;

    static appbase::abstract_plugin& _mongo_history_plugin = app().register_plugin<mongo_history_plugin>();

    struct account_history_object : public chainbase::object<account_history_object_type, account_history_object>  {
        OBJECT_CTOR( account_history_object );

        id_type      id;
        account_name account; ///< the name of the account which has this action in its history
        uint64_t     action_sequence_num = 0; ///< the sequence number of the relevant action (global)
        int32_t      account_sequence_num = 0; ///< the sequence number for this account (per-account)
    };

    struct action_history_object : public chainbase::object<action_history_object_type, action_history_object> {

        OBJECT_CTOR( action_history_object, (packed_action_trace) );

        id_type      id;
        uint64_t     action_sequence_num; ///< the sequence number of the relevant action

        shared_string        packed_action_trace;
        uint32_t             block_num;
        block_timestamp_type block_time;
        transaction_id_type  trx_id;
    };
    using account_history_id_type = account_history_object::id_type;
    using action_history_id_type  = action_history_object::id_type;

    struct by_action_sequence_num;
    struct by_account_action_seq;
    struct by_trx_id;

    using action_history_index = chainbase::shared_multi_index_container<
        action_history_object,
        indexed_by<
          ordered_unique<tag<by_id>, member<action_history_object, action_history_object::id_type, &action_history_object::id>>,
          ordered_unique<tag<by_action_sequence_num>, member<action_history_object, uint64_t, &action_history_object::action_sequence_num>>,
          ordered_unique<tag<by_trx_id>,
              composite_key< action_history_object,
                member<action_history_object, transaction_id_type, &action_history_object::trx_id>,
                member<action_history_object, uint64_t, &action_history_object::action_sequence_num >
              >
          >
        >
    >;

    using account_history_index = chainbase::shared_multi_index_container<
        account_history_object,
        indexed_by<
          ordered_unique<tag<by_id>, member<account_history_object, account_history_object::id_type, &account_history_object::id>>,
          ordered_unique<tag<by_account_action_seq>,
              composite_key< account_history_object,
                member<account_history_object, account_name, &account_history_object::account >,
                member<account_history_object, int32_t, &account_history_object::account_sequence_num >
              >
          >
        >
    >;

  } /// namespace eosio

  CHAINBASE_SET_INDEX_TYPE(eosio::account_history_object, eosio::account_history_index)
  CHAINBASE_SET_INDEX_TYPE(eosio::action_history_object, eosio::action_history_index)

  namespace eosio {
  
    class mongo_history_plugin_impl {
        public:          
          chain_plugin*          chain_plug = nullptr;
          fc::optional<scoped_connection> applied_transaction_connection;

    mongo_history_plugin::mongo_history_plugin()
    :my(std::make_shared<mongo_history_plugin_impl>()) {
    }

    mongo_history_plugin::~mongo_history_plugin() {
    }



    void mongo_history_plugin::set_program_options(options_description& cli, options_description& cfg) {
        cfg.add_options()
         ("mongodb-uri,m", bpo::value<std::string>(),
         "MongoDB URI connection string, see: https://docs.mongodb.com/master/reference/connection-string/."
               " If not specified then plugin is disabled. Default database 'EOS' is used if not specified in URI."
               " Example: mongodb://127.0.0.1:27017/EOS")
        ; 
    }

    void mongo_history_plugin::plugin_initialize(const variables_map& options) {
        ilog( "Welcome to the THUNDERDOME!!!!!" );
        try {
          if( options.count( "mongodb-uri" )) {
            std::string uri_str = options.at( "mongodb-uri" ).as<std::string>();
            ilog( "connecting to ${u}", ("u", uri_str));
            mongocxx::uri uri = mongocxx::uri{uri_str};
            my->db_name = uri.database();
            if( my->db_name.empty())
                my->db_name = "EOS";
            my->mongo_pool.emplace(uri);
          } else {
            wlog( "eosio::mongo_db_plugin configured, but no --mongodb-uri specified." );
            wlog( "mongo_db_plugin disabled." );
          }
        }FC_LOG_AND_RETHROW()
    }

    void mongo_history_plugin::plugin_startup() {
    }

    void mongo_history_plugin::plugin_shutdown() {
        my->applied_transaction_connection.reset();
    }


    namespace mongo_history_apis {
        read_only::get_actions_result read_only::get_actions( const read_only::get_actions_params& params )const {
          ilog("get_transaction_result");
          edump((params));
          get_actions_result result;
          return result;
        }


        read_only::get_transaction_result read_only::get_transaction( const read_only::get_transaction_params& p )const {
          ilog("get_transaction_result");
          get_transaction_result result;
          bool found = true;
          
          if (!found) {
            EOS_THROW(tx_not_found, "Transaction ${id} not found in history or in block number ${n}", ("id",p.id)("n", *p.block_num_hint));
          }

          return result;
        }

        read_only::get_key_accounts_results read_only::get_key_accounts(const get_key_accounts_params& params) const {
          ilog("get_key_accounts_results");
          get_key_accounts_results result;
          return result;
        }

        read_only::get_controlled_accounts_results read_only::get_controlled_accounts(const get_controlled_accounts_params& params) const {
          ilog("get_controlled_accounts_results");
          get_controlled_accounts_results result;
          return result;
        }

    } /// mongo_history_apis

} /// namespace eosio
