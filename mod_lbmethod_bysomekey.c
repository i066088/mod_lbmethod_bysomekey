/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mod_proxy.h"
#include "scoreboard.h"
#include "ap_mpm.h"
#include "apr_version.h"
#include "ap_hooks.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <apr_general.h>
#include <apr_tables.h>
#include <apr_strings.h>
#include <apr_general.h>
#include <apr_tables.h>

module AP_MODULE_DECLARE_DATA lbmethod_bysomekey_module;


//https://github.com/abrandoned/murmur2/blob/master/MurmurHash2.c
#include <stdint.h>
#define BIG_CONSTANT(x) (x##LLU)
#define MurmurHash MurmurHash64A
#define HashSeed  0xEE6B27EB
uint64_t MurmurHash64A(const void * key, int len, uint64_t seed)
{
	//	const uint64_t m = BIG_CONSTANT(0xc6a4a7935bd1e995);
	const uint64_t m = 0xc6a4a7935bd1e995;
	const int r = 47;

	uint64_t h = seed ^ (len * m);
	unsigned char* data2;

	const uint64_t * data = (const uint64_t *)key;
	const uint64_t * end = data + (len / 8);

	while (data != end)
	{
		uint64_t k = *data++;

		k *= m;
		k ^= k >> r;
		k *= m;

		h ^= k;
		h *= m;
	}
	data2 = (unsigned char*)data;

	switch (len & 7)
	{
	case 7: h ^= ((uint64_t)data2[6]) << 48;
	case 6: h ^= ((uint64_t)data2[5]) << 40;
	case 5: h ^= ((uint64_t)data2[4]) << 32;
	case 4: h ^= ((uint64_t)data2[3]) << 24;
	case 3: h ^= ((uint64_t)data2[2]) << 16;
	case 2: h ^= ((uint64_t)data2[1]) << 8;
	case 1: h ^= ((uint64_t)data2[0]);
		h *= m;
	};

	h ^= h >> r;
	h *= m;
	h ^= h >> r;

	return h;
}


/* Retrieve the parameter with the given name
* Something like 'JSESSIONID=12345...N'
*/
static char *get_path_param(apr_pool_t *pool, char *url,
	const char *name, int scolon_sep)
{
	char *path = NULL;
	char *pathdelims = "?&";

	if (scolon_sep) {
		pathdelims = ";?&";
	}
	for (path = strstr(url, name); path; path = strstr(path + 1, name)) {
		path += strlen(name);
		if (*path == '=') {
			/*
			* Session path was found, get its value
			*/
			++path;
			if (*path) {
				char *q;
				path = apr_strtok(apr_pstrdup(pool, path), pathdelims, &q);
				return path;
			}
		}
	}
	return NULL;
}

static char *get_cookie_param(request_rec *r, const char *name)
{
	const char *cookies;
	const char *start_cookie;

	if ((cookies = apr_table_get(r->headers_in, "Cookie"))) {
		for (start_cookie = ap_strstr_c(cookies, name); start_cookie;
			start_cookie = ap_strstr_c(start_cookie + 1, name)) {
			if (start_cookie == cookies ||
				start_cookie[-1] == ';' ||
				start_cookie[-1] == ',' ||
				isspace(start_cookie[-1])) {

				start_cookie += strlen(name);
				while (*start_cookie && isspace(*start_cookie))
					++start_cookie;
				if (*start_cookie++ == '=' && *start_cookie) {
					/*
					* Session cookie was found, get its value
					*/
					char *end_cookie, *cookie;
					cookie = apr_pstrdup(r->pool, start_cookie);
					if ((end_cookie = strchr(cookie, ';')) != NULL)
						*end_cookie = '\0';
					if ((end_cookie = strchr(cookie, ',')) != NULL)
						*end_cookie = '\0';
					return cookie;
				}
			}
		}
	}
	return NULL;
}


static apr_size_t get_busyfactor(proxy_balancer *balancer, int nNodeID)
{
	int i;
	proxy_worker** worker;

	worker = (proxy_worker **)balancer->workers->elts;
	for (i = 0; i < balancer->workers->nelts; i++, worker++) {
		if (i == nNodeID) {
			return (*worker)->s->busy;
		}
	}
	return -1;
}

static int get_node_data(proxy_balancer *balancer, const char* val)
{
	const apr_size_t ACCEPTABLE_BUSY_NESS = 6;

	uint64_t hash;
	short* node_hash = 0;
	short slot = 0;
	short node_id = -1;
	short node_pos = -1;

	SNodeData* node_data = 0;
	apr_size_t least_busy = UINTMAX_MAX;
	apr_size_t busy;

	if (!val) return -1;

	hash = MurmurHash(val, strlen(val), HashSeed);
	slot = (short)(hash % COMPANY_HASH_SIZE);
	node_hash = (short*)(balancer->s->node_hash);
	node_pos = node_hash[slot];

	while (node_pos != -1) {
		node_data = (SNodeData*)(balancer->s->node_block) + node_pos;
		if (hash == node_data->hash) {
			if (strncmp(val, node_data->company, sizeof(node_data->company)) == 0) {
				busy = get_busyfactor(balancer, node_data->node);
				if (busy < least_busy) {
					least_busy = busy;
					node_id = node_data->node;
				}
			}
		}
		node_pos = node_data->next;
	}

	if (node_id != -1 && least_busy > ACCEPTABLE_BUSY_NESS) {
		int i;
		proxy_worker** worker;
		worker = (proxy_worker **)balancer->workers->elts;
		for (i = 0; i < balancer->workers->nelts; ++i, worker++) {
			if ((*worker)->s->busy < ACCEPTABLE_BUSY_NESS / 2) {
				node_id = -1;
				break;
			}
		}
	}
	return node_id;
}

static int add_node_data(proxy_balancer *balancer, const char* val, int nodeid)
{
	uint64_t hash;
	short* node_hash = 0;
	short slot = 0;
	SNodeData* node_data = 0;

	if (!val || balancer->s->curNodeDataNum >= NODE_DATA_NUM) return -1;

	hash = MurmurHash(val, strlen(val), HashSeed);
	slot = (short)(hash % COMPANY_HASH_SIZE);
	node_hash = (short*)(balancer->s->node_hash);

	node_data = (SNodeData*)(balancer->s->node_block) + balancer->s->curNodeDataNum;
	node_data->hash = hash;
	node_data->next = node_hash[slot];
	node_data->node = nodeid;

	strncpy(node_data->company, val, VALUE_BUFFER_SIZE - 1);
	node_hash[slot] = balancer->s->curNodeDataNum;
	balancer->s->curNodeDataNum++;

	return balancer->s->curNodeDataNum;
}

static proxy_worker* find_empty_node(proxy_balancer *balancer)
{
	int i;
	proxy_worker ** worker;

	worker = (proxy_worker **)balancer->workers->elts;
	for (i = 0; i < balancer->workers->nelts; i++, worker++) {
		if ((*worker)->s->elected == 0) {
			return *worker;
		}
	}
	return NULL;
}

static int is_best_bysomekey(proxy_worker *current, proxy_worker *prev_best, void *baton)
{
    int *total_factor = (int *)baton;

    current->s->lbstatus += current->s->lbfactor;
    *total_factor += current->s->lbfactor;

    return (
        !prev_best
        || (current->s->busy < prev_best->s->busy)
        || (
            (current->s->busy == prev_best->s->busy)
            && (current->s->lbstatus > prev_best->s->lbstatus)
        )
    );
}

static proxy_worker *find_best_bysomekey(proxy_balancer *balancer,
                                          request_rec *r)
{
	int node_id = -1;
	int i = 0;

	const char* key = "SomeKey";
	const char* val = NULL;
	const char* cookie = NULL;

	proxy_worker **worker = NULL;
	proxy_worker* candidate = NULL;

	val = get_cookie_param(r, key);
	if (!val)
	{
		val = apr_table_get(r->headers_in, key);
		if (!val) {
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
				"No key found in the request header"
			);
			if (r->args) {
				val = get_path_param(r->pool, r->args, key, balancer->s->scolonsep);
			}
		}
	}

	if (!val) {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
			"No key found in the request query path"
		);
	}
	else {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
			"Company '%s' found in the request header or query path.", val);

		node_id = get_node_data(balancer, val);
		if (node_id == -1) {
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
				"Node for key '%s' not found in cache or the node is busy", val);
		}
		else {
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
				"Node %d ==> key '%s' found in cache.", node_id, val);
			worker = (proxy_worker **)balancer->workers->elts;
			for (i = 0; i < balancer->workers->nelts; i++, worker++) {
				if (node_id == i) {
					candidate = *worker;
					ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
						"Node %d is selected as candidate !",
						node_id);
					break;
				}
			}
		}
	}

	//if (val) {
	//	if (candidate == NULL) {
	//		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
	//			"Trying to find a candidate with empty request processing number ..."
	//		);

	//		candidate = find_empty_node(balancer);
	//		if (candidate == NULL) {
	//			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
	//				"No empty node, trying to find a candidate based on the balancing algorithm ..."
	//			);
	//		}
	//	}
	//}

	if (!val || candidate == NULL) {
		int total_factor = 0;
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
			"Trying to find a candidate based on the bysomekey algorithm ...");
		candidate =
			ap_proxy_balancer_get_best_worker(balancer, r, is_best_bysomekey,
				&total_factor);

		if (candidate) {
			candidate->s->lbstatus -= total_factor;
		}
	}

	if (candidate != NULL && val) {
		if (node_id == -1) {
			ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
				"Trying to find the node id of the candidate..."
			);
			worker = (proxy_worker **)balancer->workers->elts;
			for (i = 0; i < balancer->workers->nelts; i++, worker++) {
				if (candidate == *worker) {
					node_id = i;
					ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
						"The node id for the candidate is %d.", node_id);
					break;
				}
			}

			if (node_id != -1) {
				ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, APLOGNO(01159)
					"Save the node id %d in cache.", node_id);
				add_node_data(balancer, val, node_id);
			}
		}
	}
    return candidate;
}

/* assumed to be mutex protected by caller */
static apr_status_t reset(proxy_balancer *balancer, server_rec *s)
{
    int i;
    proxy_worker **worker;
    worker = (proxy_worker **)balancer->workers->elts;
    for (i = 0; i < balancer->workers->nelts; i++, worker++) {
        (*worker)->s->lbstatus = 0;
        (*worker)->s->busy = 0;

		memset(balancer->s->node_block, 0, sizeof(balancer->s->node_block));
		memset(balancer->s->node_hash, -1, sizeof(balancer->s->node_hash));
		balancer->s->curNodeDataNum = 0;
    }
    return APR_SUCCESS;
}

static apr_status_t age(proxy_balancer *balancer, server_rec *s)
{
    return APR_SUCCESS;
}

static const proxy_balancer_method bysomekey =
{
    "bysomekey",
    &find_best_bysomekey,
    NULL,
    &reset,
    &age,
    NULL
};

static void register_hook(apr_pool_t *p)
{
    ap_register_provider(p, PROXY_LBMETHOD, "bysomekey", "0", &bysomekey);
}

AP_DECLARE_MODULE(lbmethod_bysomekey) = {
    STANDARD20_MODULE_STUFF,
    NULL,       /* create per-directory config structure */
    NULL,       /* merge per-directory config structures */
    NULL,       /* create per-server config structure */
    NULL,       /* merge per-server config structures */
    NULL,       /* command apr_table_t */
    register_hook /* register hooks */
};
