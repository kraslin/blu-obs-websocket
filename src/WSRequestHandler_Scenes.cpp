#include "Utils.h"

#include "WSRequestHandler.h"

/**
* @typedef {Object} `Scene`
* @property {String} `name` Name of the currently active scene.
* @property {Array<SceneItem>} `sources` Ordered list of the current scene's source items.
*/

/**
 * Switch to the specified scene.
 *
 * @param {String} `scene-name` Name of the scene to switch to.
 *
 * @api requests
 * @name SetCurrentScene
 * @category scenes
 * @since 0.3
 */
HandlerResponse WSRequestHandler::HandleSetCurrentScene(WSRequestHandler* req) {
	if (!req->hasField("scene-name")) {
		return req->SendErrorResponse("missing request parameters");
	}

	const char* sceneName = obs_data_get_string(req->data, "scene-name");
	OBSSourceAutoRelease source = obs_get_source_by_name(sceneName);

	if (source) {
		obs_frontend_set_current_scene(source);
		return req->SendOKResponse();
	} else {
		return req->SendErrorResponse("requested scene does not exist");
	}
}

/**
 * Get the current scene's name and source items.
 * 
 * @return {String} `name` Name of the currently active scene.
 * @return {Array<SceneItem>} `sources` Ordered list of the current scene's source items.
 *
 * @api requests
 * @name GetCurrentScene
 * @category scenes
 * @since 0.3
 */
HandlerResponse WSRequestHandler::HandleGetCurrentScene(WSRequestHandler* req) {
	OBSSourceAutoRelease currentScene = obs_frontend_get_current_scene();
	OBSDataArrayAutoRelease sceneItems = Utils::GetSceneItems(currentScene);

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_string(data, "name", obs_source_get_name(currentScene));
	obs_data_set_array(data, "sources", sceneItems);

	return req->SendOKResponse(data);
}

HandlerResponse WSRequestHandler::HandleGetScene(WSRequestHandler* req) {
	if (!req->hasField("scene-name")) {
		return req->SendErrorResponse("Scene name not specified");
	}

	QString sceneName = obs_data_get_string(req->data, "scene-name");
	OBSSourceAutoRelease scene_src = obs_get_source_by_name(sceneName.toUtf8());

	if (!scene_src) {
		return req->SendErrorResponse("Scene does not exist");
	}

	OBSDataArrayAutoRelease sceneItems = Utils::GetSceneItems(scene_src);

	OBSDataAutoRelease response = obs_data_create();
	obs_data_set_array(response, "sources", sceneItems);

	return req->SendOKResponse(response);
}

/**
 * Get a list of scenes in the currently active profile.
 * 
 * @return {String} `current-scene` Name of the currently active scene.
 * @return {Array<Scene>} `scenes` Ordered list of the current profile's scenes (See `[GetCurrentScene](#getcurrentscene)` for more information).
 *
 * @api requests
 * @name GetSceneList
 * @category scenes
 * @since 0.3
 */
HandlerResponse WSRequestHandler::HandleGetSceneList(WSRequestHandler* req) {
	OBSSourceAutoRelease currentScene = obs_frontend_get_current_scene();
	OBSDataArrayAutoRelease scenes = Utils::GetScenes();

	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_string(data, "current-scene",
		obs_source_get_name(currentScene));
	obs_data_set_array(data, "scenes", scenes);

	return req->SendOKResponse(data);
}

/**
* Changes the order of scene items in the requested scene.
*
* @param {String (optional)} `scene` Name of the scene to reorder (defaults to current).
* @param {Array<Scene>} `items` Ordered list of objects with name and/or id specified. Id preferred due to uniqueness per scene
* @param {int (optional)} `items[].id` Id of a specific scene item. Unique on a scene by scene basis.
* @param {String (optional)} `items[].name` Name of a scene item. Sufficiently unique if no scene items share sources within the scene.
*
* @api requests
* @name ReorderSceneItems
* @category scenes
* @since 4.5.0
*/
HandlerResponse WSRequestHandler::HandleReorderSceneItems(WSRequestHandler* req) {
	QString sceneName = obs_data_get_string(req->data, "scene");
	OBSScene scene = Utils::GetSceneFromNameOrCurrent(sceneName);
	if (!scene) {
		return req->SendErrorResponse("requested scene doesn't exist");
	}

	OBSDataArrayAutoRelease items = obs_data_get_array(req->data, "items");
	if (!items) {
		return req->SendErrorResponse("sceneItem order not specified");
	}

	struct reorder_context {
		obs_data_array_t* items;
		bool success;
		QString errorMessage;
	};

	struct reorder_context ctx;
	ctx.success = false;
	ctx.items = items;

	obs_scene_atomic_update(scene, [](void* param, obs_scene_t* scene) {
		auto ctx = reinterpret_cast<struct reorder_context*>(param);

		QVector<struct obs_sceneitem_order_info> orderList;
		struct obs_sceneitem_order_info info;

		size_t itemCount = obs_data_array_count(ctx->items);
		for (int i = 0; i < itemCount; i++) {
			OBSDataAutoRelease item = obs_data_array_item(ctx->items, i);

			OBSSceneItemAutoRelease sceneItem = Utils::GetSceneItemFromItem(scene, item);
			if (!sceneItem) {
				ctx->success = false;
				ctx->errorMessage = "Invalid sceneItem id or name specified";
				return;
			}

			info.group = nullptr;
			info.item = sceneItem;
			orderList.insert(0, info);
		}

		ctx->success = obs_scene_reorder_items2(scene, orderList.data(), orderList.size());
		if (!ctx->success) {
			ctx->errorMessage = "Invalid sceneItem order";
		}
	}, &ctx);

	if (!ctx.success) {
		return req->SendErrorResponse(ctx.errorMessage);
	}

	return req->SendOKResponse();
}

HandlerResponse WSRequestHandler::HandleSetSceneItemIndex(WSRequestHandler* req) {
	if (!req->hasField("scene-name")) {
		return req->SendErrorResponse("Scene name not specified");
	}

	if (!req->hasField("item")) {
		return req->SendErrorResponse("Item name not specified");
	}

	if (!req->hasField("index")) {
		return req->SendErrorResponse("Item index not specified");
	}

	QString sceneName = obs_data_get_string(req->data, "scene-name");
	obs_source_t* scene_src = obs_get_source_by_name(sceneName.toUtf8());
	if (!scene_src) {
		return req->SendErrorResponse("Scene does not exist");
	}

	obs_scene_t* scene = obs_scene_from_source(scene_src);
	if (!scene) {
		return req->SendErrorResponse("Unable to load scene");
	}

	QString srcName = obs_data_get_string(req->data, "item");
	obs_sceneitem_t* sceneItem = obs_scene_find_source(scene, srcName.toUtf8());
	if(!sceneItem) {
		return req->SendErrorResponse("Unable to find item in scene");
	}

	int pos = obs_data_get_int(req->data, "index");
	obs_sceneitem_set_order_position(sceneItem, pos);

	return req->SendOKResponse();
}

HandlerResponse WSRequestHandler::HandleSetSceneItemOrder(WSRequestHandler* req) {
	obs_order_movement where;

	if (!req->hasField("scene-name")) {
		return req->SendErrorResponse("Scene name must be specified");
	}
	QString sceneName = obs_data_get_string(req->data, "scene-name");
	obs_source_t* scene_src = obs_get_source_by_name(sceneName.toUtf8());

	if (!scene_src) {
		return req->SendErrorResponse("Scene does not exist");
	}

	obs_scene_t* scene = obs_scene_from_source(scene_src);
	if (!scene) {
		return req->SendErrorResponse("Unable to find scene");
	}

	if (!req->hasField("item")) {
		return req->SendErrorResponse("Item is not specified");
	}

	QString sourceName = obs_data_get_string(req->data, "item");
	obs_sceneitem_t *item = obs_scene_find_source(scene, sourceName.toUtf8());
	if (!item) {
		return req->SendErrorResponse("Unable to find source in scene");
	}

	if (!req->hasField("order")) {
		return req->SendErrorResponse("Invalid order specified");
	}
	QString newOrder = obs_data_get_string(req->data, "order");
	
	if (newOrder == "OBS_ORDER_MOVE_UP") {
		where = OBS_ORDER_MOVE_UP;
	}
	else if (newOrder == "OBS_ORDER_MOVE_DOWN") {
		where = OBS_ORDER_MOVE_DOWN;
	}
	else if (newOrder == "OBS_ORDER_MOVE_TOP") {
		where = OBS_ORDER_MOVE_TOP;
	}
	else if (newOrder == "OBS_ORDER_MOVE_BOTTOM") {
		where = OBS_ORDER_MOVE_BOTTOM;
	}
	else {
		return req->SendErrorResponse("Unknown order");
	}

	obs_sceneitem_set_order(item, where);
	obs_source_release(scene_src);
	
	OBSDataAutoRelease response = obs_data_create();
	obs_data_set_string(response, "source", obs_data_get_string(req->data, "source"));
	obs_data_set_string(response, "scene", obs_data_get_string(req->data, "scene"));

	return req->SendOKResponse(response);
}
