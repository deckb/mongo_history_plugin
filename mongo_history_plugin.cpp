#include <eosio/mongo_history_plugin/mongo_history_plugin.hpp>
#include <eosio/mongo_history_plugin/account_control_history_object.hpp>
#include <eosio/mongo_history_plugin/public_key_history_object.hpp>
#include <eosio/chain/controller.hpp>
#include <eosio/chain/trace.hpp>
#include <eosio/chain_plugin/chain_plugin.hpp>

#include <fc/io/json.hpp>

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

    template<typename MultiIndex, typename LookupType>
    static void remove(chainbase::database& db, const account_name& account_name, const permission_name& permission)
    {
        const auto& idx = db.get_index<MultiIndex, LookupType>();
        auto& mutable_idx = db.get_mutable_index<MultiIndex>();
        while(!idx.empty()) {
          auto key = boost::make_tuple(account_name, permission);
          const auto& itr = idx.lower_bound(key);
          if (itr == idx.end())
              break;

          const auto& range_end = idx.upper_bound(key);
          if (itr == range_end)
              break;

          mutable_idx.remove(*itr);
        }
    }

    static void add(chainbase::database& db, const vector<key_weight>& keys, const account_name& name, const permission_name& permission)
    {
        for (auto pub_key_weight : keys ) {
          db.create<public_key_history_object>([&](public_key_history_object& obj) {
              obj.public_key = pub_key_weight.key;
              obj.name = name;
              obj.permission = permission;
          });
        }
    }

    static void add(chainbase::database& db, const vector<permission_level_weight>& controlling_accounts, const account_name& account_name, const permission_name& permission)
    {
        for (auto controlling_account : controlling_accounts ) {
          db.create<account_control_history_object>([&](account_control_history_object& obj) {
              obj.controlled_account = account_name;
              obj.controlled_permission = permission;
              obj.controlling_account = controlling_account.permission.actor;
          });
        }
    }

    struct filter_entry {
        name receiver;
        name action;
        name actor;

        std::tuple<name, name, name> key() const {
          return std::make_tuple(receiver, action, actor);
        }

        friend bool operator<( const filter_entry& a, const filter_entry& b ) {
          return a.key() < b.key();
        }
    };

    class mongo_history_plugin_impl {
        public:
          bool bypass_filter = false;
          std::set<filter_entry> filter_on;
          std::set<filter_entry> filter_out;
          chain_plugin*          chain_plug = nullptr;
          fc::optional<scoped_connection> applied_transaction_connection;

            bool filter(const action_trace& act) {
              bool pass_on = false;
              if (bypass_filter) {
                pass_on = true;
              }
              if (filter_on.find({ act.receipt.receiver, 0, 0 }) != filter_on.end()) {
                pass_on = true;
              }
              if (filter_on.find({ act.receipt.receiver, act.act.name, 0 }) != filter_on.end()) {
                pass_on = true;
              }
              for (const auto& a : act.act.authorization) {
                if (filter_on.find({ act.receipt.receiver, 0, a.actor }) != filter_on.end()) {
                  pass_on = true;
                }
                if (filter_on.find({ act.receipt.receiver, act.act.name, a.actor }) != filter_on.end()) {
                  pass_on = true;
                }
              }

              if (!pass_on) {  return false;  }

              if (filter_out.find({ act.receipt.receiver, 0, 0 }) != filter_out.end()) {
                return false;
              }
              if (filter_out.find({ act.receipt.receiver, act.act.name, 0 }) != filter_out.end()) {
                return false;
              }
              for (const auto& a : act.act.authorization) {
                if (filter_out.find({ act.receipt.receiver, 0, a.actor }) != filter_out.end()) {
                  return false;
                }
                if (filter_out.find({ act.receipt.receiver, act.act.name, a.actor }) != filter_out.end()) {
                  return false;
                }
              }

              return true;
            }

          set<account_name> account_set( const action_trace& act ) {
              set<account_name> result;

              result.insert( act.receipt.receiver );
              for( const auto& a : act.act.authorization ) {
                if( bypass_filter ||
                    filter_on.find({ act.receipt.receiver, 0, 0}) != filter_on.end() ||
                    filter_on.find({ act.receipt.receiver, 0, a.actor}) != filter_on.end() ||
                    filter_on.find({ act.receipt.receiver, act.act.name, 0}) != filter_on.end() ||
                    filter_on.find({ act.receipt.receiver, act.act.name, a.actor }) != filter_on.end() ) {
                  if ((filter_out.find({ act.receipt.receiver, 0, 0 }) == filter_out.end()) &&
                      (filter_out.find({ act.receipt.receiver, 0, a.actor }) == filter_out.end()) &&
                      (filter_out.find({ act.receipt.receiver, act.act.name, 0 }) == filter_out.end()) &&
                      (filter_out.find({ act.receipt.receiver, act.act.name, a.actor }) == filter_out.end())) {
                    result.insert( a.actor );
                  }
                }
              }
              return result;
          }

          void record_account_action( account_name n, const base_action_trace& act ) {
              auto& chain = chain_plug->chain();
              chainbase::database& db = const_cast<chainbase::database&>( chain.db() ); // Override read-only access to state DB (highly unrecommended practice!)

              const auto& idx = db.get_index<account_history_index, by_account_action_seq>();
              auto itr = idx.lower_bound( boost::make_tuple( name(n.value+1), 0 ) );

              uint64_t asn = 0;
              if( itr != idx.begin() ) --itr;
              if( itr->account == n )
                asn = itr->account_sequence_num + 1;

              //idump((n)(act.receipt.global_sequence)(asn));
              const auto& a = db.create<account_history_object>( [&]( auto& aho ) {
                aho.account = n;
                aho.action_sequence_num = act.receipt.global_sequence;
                aho.account_sequence_num = asn;
              });
              //idump((a.account)(a.action_sequence_num)(a.action_sequence_num));
          }

          void on_system_action( const action_trace& at ) {
              auto& chain = chain_plug->chain();
              chainbase::database& db = const_cast<chainbase::database&>( chain.db() ); // Override read-only access to state DB (highly unrecommended practice!)
              if( at.act.name == N(newaccount) )
              {
                const auto create = at.act.data_as<chain::newaccount>();
                add(db, create.owner.keys, create.name, N(owner));
                add(db, create.owner.accounts, create.name, N(owner));
                add(db, create.active.keys, create.name, N(active));
                add(db, create.active.accounts, create.name, N(active));
              }
              else if( at.act.name == N(updateauth) )
              {
                const auto update = at.act.data_as<chain::updateauth>();
                remove<public_key_history_multi_index, by_account_permission>(db, update.account, update.permission);
                remove<account_control_history_multi_index, by_controlled_authority>(db, update.account, update.permission);
                add(db, update.auth.keys, update.account, update.permission);
                add(db, update.auth.accounts, update.account, update.permission);
              }
              else if( at.act.name == N(deleteauth) )
              {
                const auto del = at.act.data_as<chain::deleteauth>();
                remove<public_key_history_multi_index, by_account_permission>(db, del.account, del.permission);
                remove<account_control_history_multi_index, by_controlled_authority>(db, del.account, del.permission);
              }
          }

          void on_action_trace( const action_trace& at ) {
              if( filter( at ) ) {
                //idump((fc::json::to_pretty_string(at)));
                auto& chain = chain_plug->chain();
                chainbase::database& db = const_cast<chainbase::database&>( chain.db() ); // Override read-only access to state DB (highly unrecommended practice!)

                db.create<action_history_object>( [&]( auto& aho ) {
                    auto ps = fc::raw::pack_size( at );
                    aho.packed_action_trace.resize(ps);
                    datastream<char*> ds( aho.packed_action_trace.data(), ps );
                    fc::raw::pack( ds, at );
                    aho.action_sequence_num = at.receipt.global_sequence;
                    aho.block_num = chain.pending_block_state()->block_num;
                    aho.block_time = chain.pending_block_time();
                    aho.trx_id     = at.trx_id;
                });

                auto aset = account_set( at );
                for( auto a : aset ) {
                    record_account_action( a, at );
                }
              }
              if( at.receipt.receiver == chain::config::system_account_name )
                on_system_action( at );
              for( const auto& iline : at.inline_traces ) {
                on_action_trace( iline );
              }
          }

          void on_applied_transaction( const transaction_trace_ptr& trace ) {
              for( const auto& atrace : trace->action_traces ) {
                on_action_trace( atrace );
              }
          }
    };

    mongo_history_plugin::mongo_history_plugin()
    :my(std::make_shared<mongo_history_plugin_impl>()) {
    }

    mongo_history_plugin::~mongo_history_plugin() {
    }



    void mongo_history_plugin::set_program_options(options_description& cli, options_description& cfg) {
        //cfg.add_options()
        //      ("filter-on,f", bpo::value<vector<string>>()->composing(),
        //       "Track actions which match receiver:action:actor. Actor may be blank to include all. Action and Actor both blank allows all from Recieiver. Receiver may not be blank.")
        //      ;
        //cfg.add_options()
        //      ("filter-out,F", bpo::value<vector<string>>()->composing(),
        //       "Do not track actions which match receiver:action:actor. Action and Actor both blank excludes all from Reciever. Actor blank excludes all from reciever:action. Receiver may not be blank.")
        //      ;
    }

    void mongo_history_plugin::plugin_initialize(const variables_map& options) {
        wlog( "Welcome to the THUNDERDOME!!!!!" );
          
    }

    void mongo_history_plugin::plugin_startup() {
    }

    void mongo_history_plugin::plugin_shutdown() {
        my->applied_transaction_connection.reset();
    }


    namespace mongo_history_apis {
        read_only::get_actions_result read_only::get_actions( const read_only::get_actions_params& params )const {
          wlog("get_transaction_result");
          edump((params));
          get_actions_result result;
          return result;
        }


        read_only::get_transaction_result read_only::get_transaction( const read_only::get_transaction_params& p )const {
          wlog("get_transaction_result");
          get_transaction_result result;
          bool found = true;
          
          if (!found) {
            EOS_THROW(tx_not_found, "Transaction ${id} not found in history or in block number ${n}", ("id",p.id)("n", *p.block_num_hint));
          }

          return result;
        }

        read_only::get_key_accounts_results read_only::get_key_accounts(const get_key_accounts_params& params) const {
          wlog("get_key_accounts_results");
          get_key_accounts_results result;
          return result;
        }

        read_only::get_controlled_accounts_results read_only::get_controlled_accounts(const get_controlled_accounts_params& params) const {
          wlog("get_controlled_accounts_results");
          get_controlled_accounts_results result;
          return result;
        }

    } /// mongo_history_apis

} /// namespace eosio
