#include <eosio/mongo_history_plugin/mongo_history_plugin.hpp>
#include <eosio/mongo_history_plugin/account_control_history_object.hpp>
#include <eosio/mongo_history_plugin/public_key_history_object.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

#include <fc/io/json.hpp>

#include <mongocxx/client.hpp>
#include <mongocxx/collection.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/pool.hpp>
#include <mongocxx/exception/operation_exception.hpp>
#include <mongocxx/exception/logic_error.hpp>

#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/stream/array.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/builder/stream/helpers.hpp>
#include <bsoncxx/types.hpp>
#include <bsoncxx/exception/exception.hpp>
#include <bsoncxx/json.hpp>
#include <bsoncxx/stdx/string_view.hpp>
#include <bsoncxx/document/value.hpp>
#include <bsoncxx/document/view.hpp>

#include <cmath>

#include <boost/algorithm/string.hpp>
//#include <boost/signals2/connection.hpp>


namespace eosio {
  using namespace chain;
  using namespace bsoncxx;
  using builder::stream::document;
  using builder::stream::open_document;
  using builder::stream::close_document;
  using builder::basic::kvp;
  using builder::basic::make_document;
  using builder::basic::make_array;
  using builder::concatenate;
  using types::b_regex;
  using types::b_document;

  class mongo_history_plugin_impl {
      public:          
        chain_plugin*          chain_plug = nullptr;
        //fc::optional<scoped_connection> applied_transaction_connection;

        std::string db_name;
        //mongocxx::instance mongo_inst;
        mongocxx::client mongo_client;

        static const std::string trans_col;
        static const std::string trans_traces_col;
        static const std::string pub_keys_col;
        static const std::string account_controls_col;
    };

    const std::string mongo_history_plugin_impl::trans_col = "transactions";
    const std::string mongo_history_plugin_impl::trans_traces_col = "transaction_traces";
    const std::string mongo_history_plugin_impl::pub_keys_col = "pub_keys";
    const std::string mongo_history_plugin_impl::account_controls_col = "account_controls";

    mongo_history_plugin::mongo_history_plugin()
    :my(std::make_shared<mongo_history_plugin_impl>()) {
    }

    mongo_history_plugin::~mongo_history_plugin() {
    }

    void mongo_history_plugin::set_program_options(options_description& cli, options_description& cfg) {
        cfg.add_options()
         ("history-mongodb-uri,m", bpo::value<std::string>(),
          "MongoDB URI connection string, see: https://docs.mongodb.com/master/reference/connection-string/."
              " If not specified then plugin is disabled. Default database 'EOS' is used if not specified in URI."
              " Example: mongodb://127.0.0.1:27017/EOS")
        ; 
    }

    void mongo_history_plugin::plugin_initialize(const variables_map& options) {
        ilog( "Welcome to the THUNDERDOME!!!!!" );
        try {
          if( options.count( "history-mongodb-uri" )) {
            std::string uri_str = options.at( "history-mongodb-uri" ).as<std::string>();
            ilog( "connecting to ${u}", ("u", uri_str));
            mongocxx::uri uri = mongocxx::uri{uri_str};
            my->db_name = uri.database();
            if( my->db_name.empty())
            my->mongo_client = {uri};
                my->db_name = "EOS";
          } else {
            wlog( "eosio::mongo_history_plugin configured, but no --history-mongodb-uri specified." );
            wlog( "mongo_history_plugin disabled." );
          }
          // init chain plugin
          my->chain_plug = app().find_plugin<chain_plugin>();
          EOS_ASSERT( my->chain_plug, chain::missing_chain_plugin_exception, ""  );
        }FC_LOG_AND_RETHROW()
    }

    void mongo_history_plugin::plugin_startup() {
    }

    void mongo_history_plugin::plugin_shutdown() {
        //my->applied_transaction_connection.reset();
    }

    namespace mongo_history_apis {

        read_only::get_actions_result read_only::get_actions( const read_only::get_actions_params& params )const {
          
          int32_t pos = params.pos ? *params.pos : -1;
          int32_t offset = params.offset ? *params.offset : -20;
         
          auto name = params.account_name;
          wdump((pos));
          wdump((offset));
          int32_t sort = 1;
          if( pos < 0 ) {
            // if negative need to reverse sort by id
            // and subtract by one to make it zero-indexed
            sort = -1;
            pos += 1;
          }
          /*int32_t end = 0;
          int32_t end = 0;
          if( offset > 0 ) {
            start = pos;
            end   = start + offset;
          } else {
            start = pos + offset;
            if( start > pos ) start = 0;
            end   = pos;
          }
          EOS_ASSERT( end >= start, chain::plugin_exception, "end position is earlier than start position" );

          idump((start)(end));*/
          // get trx traces
          auto trans_trace = history->mongo_client[history->db_name][history->trans_traces_col];
          // create options and query docs
          mongocxx::options::find opts;
          opts.sort(make_document(kvp("_id", sort)));
          if(pos != 0 || pos != -1) opts.skip(abs(pos));
          auto abs_offset = abs(offset);
          opts.limit(abs_offset);
          bsoncxx::document::value actions_query = make_document(kvp("$or", 
                      make_array(make_document(kvp("action_traces.act.authorization.actor", string(name))),
                                 make_document(kvp("action_traces.inline_traces.receipt.receiver", string(name))),
                                 make_document(kvp("action_traces.receipt.receiver", string(name)))
                      )));
          wlog("actions_query: ${s}", ("s", bsoncxx::to_json(actions_query)));
          auto cursor = trans_trace.find(actions_query.view(), opts);
          //auto doc_count = trans_trace.count_documents(actions_query);
          //ilog("cursor.count ${u}", ("u", cursor.size()));
          get_actions_result result;
          auto& chain = history->chain_plug->chain();
          result.last_irreversible_block = chain.last_irreversible_block_num();
          int32_t count = 0;
          auto start_time = fc::time_point::now();
          auto end_time = start_time;
          for(auto&& doc : cursor){           
            //ilog("doc: ${s}", ("s", bsoncxx::to_json(doc)));
            auto ele = doc["action_traces"];
            if (ele && ele.type() == type::k_array) {
              for(auto trace : ele.get_array().value ){
                auto trace_view = trace.get_document().view();
                // check authorizations
                bool is_authorizer = false;
                auto auths = trace_view["act"]["authorization"];
                if (auths && auths.type() == type::k_array) {
                  for(auto auth : auths.get_array().value ){
                    if(auth["actor"].get_utf8().value.to_string() == name){
                      is_authorizer = true;
                      wlog("found authorizer");
                      break;
                    }
                  }
                }
                // confirm act.account and receipt.receiver contain the user
                if(trace_view["act"]["account"].get_utf8().value.to_string() == name ||
                   trace_view["receipt"]["receiver"].get_utf8().value.to_string() == name ||
                   is_authorizer )
                   {
                  result.actions.emplace_back( ordered_action_result{
                                 uint32_t(trace_view["receipt"]["global_sequence"].get_value().get_int32()),
                                 trace_view["receipt"]["global_sequence"].get_value().get_int32(),
                                 uint32_t(trace_view["block_num"].get_value().get_int32()), 
                                 chain::block_timestamp_type(fc::time_point::from_iso_string(trace_view["block_time"].get_utf8().value.to_string())),
                                 fc::json::from_string(bsoncxx::to_json(trace_view))
                                 });
                  count++;
                  if(count > abs_offset){
                    break;
                  }
                }
              } 
            }           
            // bail out 
            end_time = fc::time_point::now();
            if( end_time - start_time > fc::microseconds(100000) ) {
              result.time_limit_exceeded_error = true;
              break;
           }
          }
          return result;
        }

        read_only::get_transaction_result read_only::get_transaction( const read_only::get_transaction_params& params )const {
          // 
          auto& chain = history->chain_plug->chain();
          //transaction_id_type input_id;
          auto input_id_length = params.id.size();
          try {
            FC_ASSERT( input_id_length <= 64, "hex string is too long to represent an actual transaction id" );
            FC_ASSERT( input_id_length >= 8,  "hex string representing transaction id should be at least 8 characters long to avoid excessive collisions" );
            //input_id = transaction_id_type(params.id);
          } EOS_RETHROW_EXCEPTIONS(transaction_id_type_exception, "Invalid transaction ID: ${transaction_id}", ("transaction_id", params.id))

          //auto regex_str = b_regex("^" + params.id , "");
          auto regex_str = "^" + params.id + ".*";
          ilog("looking up id: ${s}", ("s", regex_str ));
          // get trx
          auto trans = history->mongo_client[history->db_name][history->trans_col];
          auto doc_trx = trans.find_one(make_document(kvp("trx_id", b_regex{"^" + params.id + ".*"})));
          // get trx traces
          auto trans_trace = history->mongo_client[history->db_name][history->trans_traces_col];
          auto doc_trace = trans_trace.find_one(make_document(kvp("id", b_regex{"^" + params.id + ".*"})));

          if (!doc_trx && !params.block_num_hint )  {
            EOS_THROW(tx_not_found, "Transaction ${id} not found in history or in block number ${n}", ("id",params.id)("n", *params.block_num_hint));
          }
          //ilog("doc_trx: ${s}", ("s", bsoncxx::to_json(*doc_trx)));
          //ilog("doc_trace: ${s}", ("s", bsoncxx::to_json(*doc_trace)));
          get_transaction_result result;
          if(doc_trx) {
            auto trx_view = doc_trx->view();
            auto trace_view = doc_trace->view();
            // merge receipt with trx 
            bsoncxx::builder::stream::document result_doc{};
            result_doc << "receipt" << trace_view["receipt"].get_document() << 
                          "trx" << open_document << concatenate(trx_view) << close_document;
            // setup resuls
            result.id         = transaction_id_type(trace_view["id"].get_value().get_utf8().value.to_string());
            result.last_irreversible_block = chain.last_irreversible_block_num();
            result.block_num  = trace_view["block_num"].get_value().get_int64();
            // TODO get correct time
            result.block_time = chain::block_timestamp_type(fc::time_point::from_iso_string(trace_view["block_time"].get_utf8().value.to_string()));
            // take trx json string to json
            result.trx = fc::json::from_string(bsoncxx::to_json(result_doc.view()));
            // get the inline traces
            auto ele = trace_view["action_traces"];
            if (ele && ele.type() == type::k_array) {
              for(auto trace : ele.get_array().value ){
                result.traces.emplace_back(fc::json::from_string(bsoncxx::to_json(trace.get_document().view())));
              } 
            }
          }
          else  {
            elog("TODO: doc is incorrect");
          }
          return result;
        }

        read_only::get_key_accounts_results read_only::get_key_accounts(const get_key_accounts_params& params) const {
          
          string pub_key = string(params.public_key);
          ilog("get_key_accounts_results: ${s}", ("s",pub_key));
          // get mongo collection and query
          // db.pub_keys.find({"public_key":"EOS7p6AYJLZvTWKBnPHMY3ytmXkuyY55cz7XsSpFEKTyz5pZQiBhy"}) 
          auto pub_keys = history->mongo_client[history->db_name][history->pub_keys_col];
          auto cursor = pub_keys.find(make_document(kvp("public_key", pub_key)));

          std::set<account_name> accounts;
          for(auto doc : cursor){
            ilog("doc: ${s}", ("s", bsoncxx::to_json(doc)));  
            accounts.insert(doc["account"].get_value().get_utf8().value.to_string());
          }
        
          return {vector<account_name>(accounts.begin(), accounts.end())};
        }

        read_only::get_controlled_accounts_results read_only::get_controlled_accounts(const get_controlled_accounts_params& params) const {
          
          string account = string(params.controlling_account);
          ilog("get_controlled_accounts_results: ${s}", ("s", account));
          // get mongo collection and query
          // db.account_controls.find({"controlling_account":"eosnewyorkio"})
          auto acct_ctrl = history->mongo_client[history->db_name][history->account_controls_col];
          auto cursor = acct_ctrl.find(make_document(kvp("controlling_account", account)));

          std::set<account_name> accounts;
          for(auto doc : cursor){
            ilog("doc: ${s}", ("s", bsoncxx::to_json(doc)));
            accounts.insert(doc["controlled_account"].get_value().get_utf8().value.to_string());
          }
          
          return {vector<account_name>(accounts.begin(), accounts.end())};
        }

    } /// mongo_history_apis

} /// namespace eosio
