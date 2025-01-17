/*
 * Copyright (c) 2019 oxarbitrage and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <boost/test/unit_test.hpp>

#include <graphene/app/api.hpp>
#include <graphene/utilities/tempdir.hpp>
#include <fc/crypto/digest.hpp>

#include <graphene/custom_operations/custom_operations_plugin.hpp>

#include "../common/database_fixture.hpp"

#define BOOST_TEST_MODULE Custom operations plugin tests

using namespace graphene::chain;
using namespace graphene::chain::test;
using namespace graphene::app;
using namespace graphene::custom_operations;

BOOST_FIXTURE_TEST_SUITE( custom_operation_tests, database_fixture )

void map_operation(flat_map<string, optional<string>>& pairs, bool remove, string& catalog, account_id_type& account,
      fc::ecc::private_key& pk, database& db)
{
   signed_transaction trx;
   set_expiration(db, trx);

   custom_operation op;
   account_storage_map store;

   store.key_values = pairs;
   store.remove = remove;
   store.catalog = catalog;

   auto packed = fc::raw::pack(store);
   packed.insert(packed.begin(), account_storage_object::type_id);

   op.payer = account;
   op.data = packed;
   op.fee = db.get_global_properties().parameters.current_fees->calculate_fee(op);
   trx.operations.push_back(op);
   trx.sign(pk, db.get_chain_id());
   PUSH_TX(db, trx, ~0);
   trx.clear();
}

BOOST_AUTO_TEST_CASE(custom_operations_account_storage_map_test)
{
try {
   ACTORS((maxirmx)(alice)(robert)(patty));

   app.enable_plugin("custom_operations");
   custom_operations_api custom_operations_api(app);

   generate_block();
   enable_fees();

   int64_t init_balance(10000 * GRAPHENE_BLOCKCHAIN_PRECISION);

   transfer(committee_account, maxirmx_id, asset(init_balance));
   transfer(committee_account, alice_id, asset(init_balance));

   // catalog is indexed so cant be too big(greater than CUSTOM_OPERATIONS_MAX_KEY_SIZE(200) is not allowed)
   std::string catalog(201, 'a');
   flat_map<string, optional<string>> pairs;
   pairs["key"] = fc::json::to_string("value");
   map_operation(pairs, false, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   auto storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", catalog);
   BOOST_CHECK_EQUAL(storage_results_maxirmx.size(), 0 );

   // keys are indexed so they cant be too big(greater than CUSTOM_OPERATIONS_MAX_KEY_SIZE(200) is not allowed)
   catalog = "whatever";
   std::string key(201, 'a');
   pairs.clear();
   pairs[key] = fc::json::to_string("value");
   map_operation(pairs, false, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", catalog);
   BOOST_CHECK_EQUAL(storage_results_maxirmx.size(), 0 );

   // creating a map with bad json as value is not allowed
   catalog = "whatever";
   pairs.clear();
   pairs["key"] = "value";
   map_operation(pairs, false, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", catalog);
   BOOST_CHECK_EQUAL(storage_results_maxirmx.size(), 0 );

   // maxirmx adds key-value data via custom operation to a settings catalog
   catalog = "settings";
   pairs.clear();
   pairs["language"] = fc::json::to_string("en");
   pairs["image_url"] = fc::json::to_string("http://some.image.url/img.jpg");
   map_operation(pairs, false, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   // check maxirmx stored data with the api
   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", "settings");
   BOOST_REQUIRE_EQUAL(storage_results_maxirmx.size(), 2U );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].value->as_string(), "http://some.image.url/img.jpg");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].key, "language");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].value->as_string(), "en");

   // edit some stuff and add new stuff
   pairs.clear();
   pairs["image_url"] = fc::json::to_string("http://new.image.url/newimg.jpg");
   pairs["theme"] = fc::json::to_string("dark");
   map_operation(pairs, false, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   // check old and new stuff
   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", "settings");
   BOOST_REQUIRE_EQUAL(storage_results_maxirmx.size(), 3U );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].value->as_string(), "http://new.image.url/newimg.jpg");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].key, "language");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].value->as_string(), "en");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[2].key, "theme");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[2].value->as_string(), "dark");

   // delete stuff from the storage
   pairs.clear();
   pairs["theme"] = fc::json::to_string("dark");
   map_operation(pairs, true, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   // theme is removed from the storage
   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", "settings");
   BOOST_REQUIRE_EQUAL(storage_results_maxirmx.size(), 2U );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].value->as_string(), "http://new.image.url/newimg.jpg");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].key, "language");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].value->as_string(), "en");

   // delete stuff that it is not there
   pairs.clear();
   pairs["nothere"] = fc::json::to_string("nothere");
   map_operation(pairs, true, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   // nothing changes
   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", "settings");
   BOOST_REQUIRE_EQUAL(storage_results_maxirmx.size(), 2U );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].value->as_string(), "http://new.image.url/newimg.jpg");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].key, "language");
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].value->as_string(), "en");

   // alice, duplicated keys in storage, only second value will be added
   pairs.clear();
   catalog = "random";
   pairs["key1"] = fc::json::to_string("value1");
   pairs["key1"] = fc::json::to_string("value2");
   map_operation(pairs, false, catalog, alice_id, alice_private_key, db);
   generate_block();

   vector<account_storage_object> storage_results_alice = custom_operations_api.get_storage_info("alice", "random");
   BOOST_REQUIRE_EQUAL(storage_results_alice.size(), 1U );
   BOOST_CHECK_EQUAL(storage_results_alice[0].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results_alice[0].key, "key1");
   BOOST_CHECK_EQUAL(storage_results_alice[0].value->as_string(), "value2");

   // add an object
   pairs.clear();
   catalog = "account_object";
   pairs["maxirmx"] = fc::json::to_string(maxirmx);
   map_operation(pairs, false, catalog, alice_id, alice_private_key, db);
   generate_block();

   storage_results_alice = custom_operations_api.get_storage_info("alice", "account_object");
   BOOST_REQUIRE_EQUAL(storage_results_alice.size(), 1U);
   BOOST_CHECK_EQUAL(storage_results_alice[0].account.instance.value, 17);
   BOOST_CHECK_EQUAL(storage_results_alice[0].key, "maxirmx");
   BOOST_CHECK_EQUAL(storage_results_alice[0].value->as<account_object>(20).name, "maxirmx");

   // add 2 more objects
   pairs.clear();
   catalog = "account_object";
   pairs["robert"] = fc::json::to_string(robert);
   pairs["patty"] = fc::json::to_string(patty);
   map_operation(pairs, false, catalog, alice_id, alice_private_key, db);
   generate_block();

   storage_results_alice = custom_operations_api.get_storage_info("alice", "account_object");
   BOOST_REQUIRE_EQUAL(storage_results_alice.size(), 3U);
   BOOST_CHECK_EQUAL(storage_results_alice[0].account.instance.value, 17);
   BOOST_CHECK_EQUAL(storage_results_alice[0].key, "maxirmx");
   BOOST_CHECK_EQUAL(storage_results_alice[0].value->as<account_object>(20).name, "maxirmx");
   BOOST_CHECK_EQUAL(storage_results_alice[1].account.instance.value, 17);
   BOOST_CHECK_EQUAL(storage_results_alice[1].key, "patty");
   BOOST_CHECK_EQUAL(storage_results_alice[1].value->as<account_object>(20).name, "patty");
   BOOST_CHECK_EQUAL(storage_results_alice[2].key, "robert");
   BOOST_CHECK_EQUAL(storage_results_alice[2].value->as<account_object>(20).name, "robert");

   // alice adds key-value data via custom operation to a settings catalog
   catalog = "settings";
   pairs.clear();
   pairs["image_url"] = fc::json::to_string("http://some.other.image.url/img.jpg");
   map_operation(pairs, false, catalog, alice_id, alice_private_key, db);
   generate_block();

   // test API limit config
   BOOST_CHECK_THROW( custom_operations_api.get_storage_info("alice", "account_object", {}, 7), fc::exception );

   // This does not throw
   storage_results_alice = custom_operations_api.get_storage_info("alice", "account_object", {}, 6);
   BOOST_REQUIRE_EQUAL(storage_results_alice.size(), 3U);
   BOOST_CHECK_EQUAL(storage_results_alice[0].account.instance.value, 17);
   BOOST_CHECK_EQUAL(storage_results_alice[0].key, "maxirmx");
   BOOST_CHECK_EQUAL(storage_results_alice[0].value->as<account_object>(20).name, "maxirmx");
   BOOST_CHECK_EQUAL(storage_results_alice[1].account.instance.value, 17);
   BOOST_CHECK_EQUAL(storage_results_alice[1].key, "patty");
   BOOST_CHECK_EQUAL(storage_results_alice[1].value->as<account_object>(20).name, "patty");
   BOOST_CHECK_EQUAL(storage_results_alice[2].key, "robert");
   BOOST_CHECK_EQUAL(storage_results_alice[2].value->as<account_object>(20).name, "robert");

   // query by a wrong account
   BOOST_CHECK_THROW( custom_operations_api.get_storage_info("alice1", "account_object" ), fc::exception );

   // query by account and key
   BOOST_CHECK_THROW( custom_operations_api.get_storage_info("alice", {}, "patty" ), fc::exception );

   // query by key only
   BOOST_CHECK_THROW( custom_operations_api.get_storage_info({}, {}, "patty" ), fc::exception );

   // query by account, catalog and key
   storage_results_alice = custom_operations_api.get_storage_info("alice", "account_object", "alice1");
   BOOST_CHECK_EQUAL(storage_results_alice.size(), 0 );

   storage_results_alice = custom_operations_api.get_storage_info("alice", "account_object1", "patty");
   BOOST_CHECK_EQUAL(storage_results_alice.size(), 0 );

   storage_results_alice = custom_operations_api.get_storage_info("alice", "account_object", "patty");
   BOOST_REQUIRE_EQUAL(storage_results_alice.size(), 1U );
   BOOST_CHECK_EQUAL(storage_results_alice[0].key, "patty");
   BOOST_CHECK_EQUAL(storage_results_alice[0].value->as<account_object>(20).name, "patty");

   // query by account only
   storage_results_alice = custom_operations_api.get_storage_info("alice");
   BOOST_REQUIRE_EQUAL(storage_results_alice.size(), 5U );
   BOOST_CHECK_EQUAL(storage_results_alice[0].catalog, "random");
   BOOST_CHECK_EQUAL(storage_results_alice[0].key, "key1");
   BOOST_CHECK_EQUAL(storage_results_alice[0].value->as_string(), "value2");
   BOOST_CHECK_EQUAL(storage_results_alice[1].catalog, "account_object");
   BOOST_CHECK_EQUAL(storage_results_alice[1].key, "maxirmx");
   BOOST_CHECK_EQUAL(storage_results_alice[1].value->as<account_object>(20).name, "maxirmx");
   BOOST_CHECK_EQUAL(storage_results_alice[2].catalog, "account_object");
   BOOST_CHECK_EQUAL(storage_results_alice[2].key, "patty");
   BOOST_CHECK_EQUAL(storage_results_alice[2].value->as<account_object>(20).name, "patty");
   BOOST_CHECK_EQUAL(storage_results_alice[3].catalog, "account_object");
   BOOST_CHECK_EQUAL(storage_results_alice[3].key, "robert");
   BOOST_CHECK_EQUAL(storage_results_alice[3].value->as<account_object>(20).name, "robert");
   BOOST_CHECK_EQUAL(storage_results_alice[4].catalog, "settings");
   BOOST_CHECK_EQUAL(storage_results_alice[4].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results_alice[4].value->as_string(), "http://some.other.image.url/img.jpg");

   // query by catalog only
   auto storage_results = custom_operations_api.get_storage_info({}, "settings1");
   BOOST_REQUIRE_EQUAL(storage_results.size(), 0 );

   storage_results = custom_operations_api.get_storage_info({}, "settings");
   BOOST_REQUIRE_EQUAL(storage_results.size(), 3U );
   BOOST_CHECK_EQUAL(storage_results[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results[0].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results[0].value->as_string(), "http://new.image.url/newimg.jpg");
   BOOST_CHECK_EQUAL(storage_results[1].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results[1].key, "language");
   BOOST_CHECK_EQUAL(storage_results[1].value->as_string(), "en");
   BOOST_CHECK_EQUAL(storage_results[2].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results[2].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results[2].value->as_string(), "http://some.other.image.url/img.jpg");

   // Pagination
   storage_results = custom_operations_api.get_storage_info({}, "settings", {}, 2);
   BOOST_REQUIRE_EQUAL(storage_results.size(), 2U );
   BOOST_CHECK_EQUAL(storage_results[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results[0].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results[0].value->as_string(), "http://new.image.url/newimg.jpg");
   BOOST_CHECK_EQUAL(storage_results[1].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results[1].key, "language");
   BOOST_CHECK_EQUAL(storage_results[1].value->as_string(), "en");

   account_storage_id_type storage_id { storage_results[1].id };

   storage_results = custom_operations_api.get_storage_info({}, "settings", {}, 2, storage_id);
   BOOST_REQUIRE_EQUAL(storage_results.size(), 2U );
   BOOST_CHECK_EQUAL(storage_results[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results[0].key, "language");
   BOOST_CHECK_EQUAL(storage_results[0].value->as_string(), "en");
   BOOST_CHECK_EQUAL(storage_results[1].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results[1].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results[1].value->as_string(), "http://some.other.image.url/img.jpg");

   // query by catalog and key
   storage_results = custom_operations_api.get_storage_info({}, "settings", "test");
   BOOST_REQUIRE_EQUAL(storage_results.size(), 0 );

   storage_results = custom_operations_api.get_storage_info({}, "settings1", "image_url");
   BOOST_REQUIRE_EQUAL(storage_results.size(), 0 );

   storage_results = custom_operations_api.get_storage_info({}, "settings", "image_url");
   BOOST_REQUIRE_EQUAL(storage_results.size(), 2U );
   BOOST_CHECK_EQUAL(storage_results[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results[0].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results[0].value->as_string(), "http://new.image.url/newimg.jpg");
   BOOST_CHECK_EQUAL(storage_results[1].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results[1].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results[1].value->as_string(), "http://some.other.image.url/img.jpg");

   // query all
   storage_results = custom_operations_api.get_storage_info();
   BOOST_REQUIRE_EQUAL(storage_results.size(), 6U ); // the configured limit, the first page
   BOOST_CHECK_EQUAL(storage_results[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results[0].catalog, "settings");
   BOOST_CHECK_EQUAL(storage_results[0].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results[0].value->as_string(), "http://new.image.url/newimg.jpg");
   BOOST_CHECK_EQUAL(storage_results[1].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results[1].catalog, "settings");
   BOOST_CHECK_EQUAL(storage_results[1].key, "language");
   BOOST_CHECK_EQUAL(storage_results[1].value->as_string(), "en");
   BOOST_CHECK_EQUAL(storage_results[2].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results[2].catalog, "random");
   BOOST_CHECK_EQUAL(storage_results[2].key, "key1");
   BOOST_CHECK_EQUAL(storage_results[2].value->as_string(), "value2");
   BOOST_CHECK_EQUAL(storage_results[3].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results[3].catalog, "account_object");
   BOOST_CHECK_EQUAL(storage_results[3].key, "maxirmx");
   BOOST_CHECK_EQUAL(storage_results[3].value->as<account_object>(20).name, "maxirmx");
   BOOST_CHECK_EQUAL(storage_results[4].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results[4].catalog, "account_object");
   BOOST_CHECK_EQUAL(storage_results[4].key, "patty");
   BOOST_CHECK_EQUAL(storage_results[4].value->as<account_object>(20).name, "patty");
   BOOST_CHECK_EQUAL(storage_results[5].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results[5].catalog, "account_object");
   BOOST_CHECK_EQUAL(storage_results[5].key, "robert");
   BOOST_CHECK_EQUAL(storage_results[5].value->as<account_object>(20).name, "robert");

   storage_id = storage_results[5].id;

   storage_results = custom_operations_api.get_storage_info({},{},{},{},storage_id);
   BOOST_REQUIRE_EQUAL(storage_results.size(), 2U ); // the 2nd page
   BOOST_CHECK_EQUAL(storage_results[0].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results[0].catalog, "account_object");
   BOOST_CHECK_EQUAL(storage_results[0].key, "robert");
   BOOST_CHECK_EQUAL(storage_results[0].value->as<account_object>(20).name, "robert");
   BOOST_CHECK_EQUAL(storage_results[1].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results[1].catalog, "settings");
   BOOST_CHECK_EQUAL(storage_results[1].key, "image_url");
   BOOST_CHECK_EQUAL(storage_results[1].value->as_string(), "http://some.other.image.url/img.jpg");

}
catch (fc::exception &e) {
   edump((e.to_detail_string()));
   throw;
} }

BOOST_AUTO_TEST_CASE(custom_operations_account_storage_list_test)
{
try {
   ACTORS((maxirmx)(alice)(robert)(patty));

   app.enable_plugin("custom_operations");
   custom_operations_api custom_operations_api(app);

   generate_block();
   enable_fees();

   int64_t init_balance(10000 * GRAPHENE_BLOCKCHAIN_PRECISION);

   transfer(committee_account, maxirmx_id, asset(init_balance));
   transfer(committee_account, alice_id, asset(init_balance));

   // catalog is indexed so cant be too big(greater than CUSTOM_OPERATIONS_MAX_KEY_SIZE(200) is not allowed)
   std::string catalog(201, 'a');
   flat_map<string, optional<string>> accounts;
   accounts[robert.name];
   map_operation(accounts, false, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   auto storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", catalog);
   BOOST_CHECK_EQUAL(storage_results_maxirmx.size(), 0 );

   // This throws due to API limit
   BOOST_CHECK_THROW( custom_operations_api.get_storage_info("maxirmx", catalog, {}, 120), fc::exception );

   // keys are indexed so they cant be too big(greater than CUSTOM_OPERATIONS_MAX_KEY_SIZE(200) is not allowed)
   catalog = "whatever";
   std::string value(201, 'a');
   accounts.clear();
   accounts[value];
   map_operation(accounts, false, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", catalog);
   BOOST_CHECK_EQUAL(storage_results_maxirmx.size(), 0 );

   // maxirmx add a list of accounts to storage
   accounts.clear();
   accounts[alice.name];
   accounts[robert.name];
   catalog = "contact_list";
   map_operation(accounts, false, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   // get the account list for maxirmx, check alice and robert are there
   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", "contact_list");
   BOOST_REQUIRE_EQUAL(storage_results_maxirmx.size(), 2U );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].key, alice.name);
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].key, robert.name);

   // add a value into account list already there
   accounts.clear();
   accounts[alice.name];
   map_operation(accounts, false, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   // nothing changes
   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", "contact_list");
   BOOST_REQUIRE_EQUAL(storage_results_maxirmx.size(), 2U );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].key, alice.name);
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[1].key, robert.name);

   // delete alice from the list
   accounts.clear();
   accounts[alice.name];
   map_operation(accounts, true, catalog, maxirmx_id, maxirmx_private_key, db);
   generate_block();

   // alice gone
   storage_results_maxirmx = custom_operations_api.get_storage_info("maxirmx", "contact_list");
   BOOST_CHECK_EQUAL(storage_results_maxirmx.size(), 1U );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].account.instance.value, 16 );
   BOOST_CHECK_EQUAL(storage_results_maxirmx[0].key, robert.name);

   // duplicated accounts in the list, only 1 will be inserted
   accounts.clear();
   accounts[robert.name];
   accounts[robert.name];
   map_operation(accounts, false, catalog, alice_id, alice_private_key, db);
   generate_block();

   auto storage_results_alice = custom_operations_api.get_storage_info("alice", "contact_list");
   BOOST_CHECK_EQUAL(storage_results_alice.size(), 1U );
   BOOST_CHECK_EQUAL(storage_results_alice[0].account.instance.value, 17 );
   BOOST_CHECK_EQUAL(storage_results_alice[0].key, robert.name);
}
catch (fc::exception &e) {
   edump((e.to_detail_string()));
   throw;
} }

BOOST_AUTO_TEST_SUITE_END()
