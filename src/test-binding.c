/*
 * Copyright (C) 2016 "IoT.bzh"
 * Author Fulup Ar Foll <fulup@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "test-binding.h"

// default api to print log when apihandle not avaliable
afb_dynapi *AFB_default;

// Config Section definition (note: controls section index should match handle
// retrieval in HalConfigExec)
static CtlSectionT ctrlSections[] = {
	{.key = "resources", .loadCB = PluginConfig},
	{.key = "onload", .loadCB = OnloadConfig},
	{.key = "events", .loadCB = EventConfig},

	{.key = NULL}
};

static void ctrlapi_ping(AFB_ReqT request) {
	static int count = 0;

	count++;
	AFB_ReqNotice(request, "Controller:ping count=%d", count);
	AFB_ReqSucess(request, json_object_new_int(count), NULL);

	return;
}

static AFB_ApiVerbs CtrlApiVerbs[] = {
	/* VERB'S NAME         FUNCTION TO CALL         SHORT DESCRIPTION */
	{.verb = "ping", .callback = ctrlapi_ping, .info = "ping test for API"},
	{.verb = NULL} /* marker for end of the array */
};

static int CtrlLoadStaticVerbs(afb_dynapi *apiHandle, AFB_ApiVerbs *verbs) {
	int errcount = 0;

	for (int idx = 0; verbs[idx].verb; idx++) {
		errcount += afb_dynapi_add_verb(
				apiHandle, CtrlApiVerbs[idx].verb, NULL, CtrlApiVerbs[idx].callback,
				(void *)&CtrlApiVerbs[idx], CtrlApiVerbs[idx].auth, 0);
	}

	return errcount;
};

static int CtrlInitOneApi(AFB_ApiT apiHandle) {
	int err = 0;
	AFB_default = apiHandle; // hugely hack to make all V2 AFB_DEBUG to work in fileutils

	// retrieve section config from api handle
	CtlConfigT *ctrlConfig = (CtlConfigT *)afb_dynapi_get_userdata(apiHandle);
	err = CtlConfigExec(apiHandle, ctrlConfig);
	if(err) {
		AFB_ApiError(apiHandle, "Error at CtlConfigExec step");
		return err;
	}

	return err;
}

// next generation dynamic API-V3 mode
#include <signal.h>

static int CtrlLoadOneApi(void *cbdata, AFB_ApiT apiHandle) {
	CtlConfigT *ctrlConfig = (CtlConfigT *)cbdata;

	// save closure as api's data context
	afb_dynapi_set_userdata(apiHandle, ctrlConfig);

	// add static controls verbs
	int err = CtrlLoadStaticVerbs(apiHandle, CtrlApiVerbs);
	if (err) {
		AFB_ApiError(apiHandle, "CtrlLoadSection fail to register static V2 verbs");
		return ERROR;
	}

	// load section for corresponding API
	err = CtlLoadSections(apiHandle, ctrlConfig, ctrlSections);

	// declare an event event manager for this API;
	afb_dynapi_on_event(apiHandle, CtrlDispatchApiEvent);

	// init API function (does not receive user closure ???
	afb_dynapi_on_init(apiHandle, CtrlInitOneApi);

	afb_dynapi_seal(apiHandle);
	return err;
}

int AddLunitFrameworkResource(AFB_ApiT apihandle, CtlConfigT *ctrlConfig)
{
	int err = 0;
	json_object *luaunitFrameworkJ = NULL, *resourcesJ = NULL;
	json_object *savedJ = NULL;

	err = wrap_json_pack(&luaunitFrameworkJ, "{ss, ss, ss, ss}",
		"uid", "lunit",
		"info", "Lunit LUA xUnit framework",
		"spath", CONTROL_PLUGIN_PATH,
		"libs", "luaunit.lua");
	if(err) {
		AFB_ApiError(apihandle, "Loading LUA xUnit framework");
		return ERROR;
	}

	if(json_object_object_get_ex(ctrlConfig->configJ, "resources", &resourcesJ) &&
	json_object_is_type(resourcesJ, json_type_array)) {
		savedJ = json_object_get(json_object_array_get_idx(resourcesJ, 0));
		json_object_array_put_idx(resourcesJ, 0, luaunitFrameworkJ);
		json_object_array_add(resourcesJ, savedJ);
	}
	else {
		AFB_ApiError(apihandle, "Resources section isn't a JSON array or does not exists.");
		return ERROR;
	}

	return 0;
}

int afbBindingVdyn(afb_dynapi *apiHandle) {

	AFB_default = apiHandle;
	AFB_ApiNotice(apiHandle, "Controller in afbBindingVdyn");

	const char *dirList = getenv("CONTROL_CONFIG_PATH");
	if (!dirList)
		dirList = CONTROL_CONFIG_PATH;

	const char *configPath = CtlConfigSearch(apiHandle, dirList, "");
	if (!configPath) {
		AFB_ApiError(apiHandle, "CtlPreInit: No %s* config found in %s ", GetBinderName(), dirList);
		return ERROR;
	}

	// load config file and create API
	CtlConfigT *ctrlConfig = CtlLoadMetaData(apiHandle, configPath);
	if (!ctrlConfig) {
		AFB_ApiError(apiHandle,
			"CtrlBindingDyn No valid control config file in:\n-- %s",
			configPath);
		return ERROR;
	}

	if (!ctrlConfig->api) {
		AFB_ApiError(apiHandle,
			"CtrlBindingDyn API Missing from metadata in:\n-- %s",
			configPath);
		return ERROR;
	}

	AFB_ApiNotice(apiHandle, "Controller API='%s' info='%s'", ctrlConfig->api,
			ctrlConfig->info);


	if (AddLunitFrameworkResource(apiHandle, ctrlConfig))
		return ERROR;

	// create one API per config file (Pre-V3 return code ToBeChanged)
	int status = afb_dynapi_new_api(apiHandle, ctrlConfig->api, ctrlConfig->info, 1, CtrlLoadOneApi, ctrlConfig);

	return status;
}