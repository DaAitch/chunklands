
#include "EngineBridge.hxx"
#include "engine_bridge_util.hxx"
#include "resolver.hxx"

namespace chunklands::core {

JSValue
EngineBridge::JSCall_renderPipelineInit(JSCbi info)
{
    engine::CEWindowHandle* handle = nullptr;
    JS_ENGINE_CHECK(info.Env(), unsafe_get_handle_ptr(&handle, info.Env(), info[0]), JSValue());

    engine::CERenderPipelineInit init;
    JSObject js_init = info[1].ToObject();

    JSObject js_gbuffer = js_init.Get("gbuffer").ToObject();
    init.gbuffer.vertex_shader = js_gbuffer.Get("vertexShader").ToString();
    init.gbuffer.fragment_shader = js_gbuffer.Get("fragmentShader").ToString();

    JSObject js_lighting = js_init.Get("lighting").ToObject();
    init.lighting.vertex_shader = js_lighting.Get("vertexShader").ToString();
    init.lighting.fragment_shader = js_lighting.Get("fragmentShader").ToString();

    JSObject js_block_select = js_init.Get("blockSelect").ToObject();
    init.select_block.vertex_shader = js_block_select.Get("vertexShader").ToString();
    init.select_block.fragment_shader = js_block_select.Get("fragmentShader").ToString();

    JSObject js_sprite = js_init.Get("sprite").ToObject();
    init.sprite.vertex_shader = js_sprite.Get("vertexShader").ToString();
    init.sprite.fragment_shader = js_sprite.Get("fragmentShader").ToString();

    JSObject js_text = js_init.Get("text").ToObject();
    init.text.vertex_shader = js_text.Get("vertexShader").ToString();
    init.text.fragment_shader = js_text.Get("fragmentShader").ToString();

    return MakeEngineCall(info.Env(),
        engine_->RenderPipelineInit(handle, std::move(init)),
        create_resolver<engine::CENone>());
}

} // namespace chunklands::core