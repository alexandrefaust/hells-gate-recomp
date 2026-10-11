#include "ui_overlay.h"

#include "vulkan_device.h"

#include "Common/interface/RefCntAutoPtr.hpp"
#include "Graphics/GraphicsEngine/interface/Buffer.h"
#include "Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "Graphics/GraphicsEngine/interface/PipelineState.h"
#include "Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "Graphics/GraphicsEngine/interface/Sampler.h"
#include "Graphics/GraphicsEngine/interface/Shader.h"
#include "Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "Graphics/GraphicsEngine/interface/Texture.h"
#include "Graphics/GraphicsEngine/interface/TextureView.h"
#include "Graphics/GraphicsTools/interface/MapHelper.hpp"

#include <rex/logging/macros.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace renderer {

namespace dl = Diligent;

namespace {

std::mutex g_frame_mutex;
std::shared_ptr<const OverlayFrame> g_latest_frame;

std::mutex g_request_mutex;
std::function<void()> g_request;

void Publish(std::shared_ptr<const OverlayFrame> frame) {
  std::lock_guard lock(g_frame_mutex);
  g_latest_frame = std::move(frame);
}

std::shared_ptr<const OverlayFrame> Latest() {
  std::lock_guard lock(g_frame_mutex);
  return g_latest_frame;
}

class OverlayTexture : public rex::ui::ImmediateTexture {
 public:
  explicit OverlayTexture(std::shared_ptr<const OverlayTextureData> data)
      : ImmediateTexture(data->width, data->height), data_(std::move(data)) {}
  const std::shared_ptr<const OverlayTextureData>& data() const { return data_; }

 private:
  std::shared_ptr<const OverlayTextureData> data_;
};

constexpr char kOverlayVS[] = R"(#version 450
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(std140, binding = 0) uniform OverlayConstants { vec4 g_ScaleOffset; };
layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
void main() {
  v_uv = a_uv;
  v_color = a_color;
  gl_Position = vec4(a_pos * g_ScaleOffset.xy + g_ScaleOffset.zw, 0.0, 1.0);
}
)";

constexpr char kOverlayPS[] = R"(#version 450
layout(binding = 1) uniform texture2D g_Texture;
layout(binding = 2) uniform sampler g_Sampler;
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 0) out vec4 out_color;
void main() {
  out_color = v_color * texture(sampler2D(g_Texture, g_Sampler), v_uv);
}
)";

}  // namespace

std::unique_ptr<rex::ui::ImmediateTexture> OverlayDrawer::CreateTexture(
    uint32_t width, uint32_t height, rex::ui::ImmediateTextureFilter filter, bool,
    const uint8_t* data) {
  if (!width || !height || !data) return nullptr;
  auto texture = std::make_shared<OverlayTextureData>();
  texture->width = width;
  texture->height = height;
  texture->linear = filter == rex::ui::ImmediateTextureFilter::kLinear;
  texture->rgba.assign(data, data + size_t(width) * height * 4);
  return std::make_unique<OverlayTexture>(std::move(texture));
}

void OverlayDrawer::Begin(rex::ui::UIDrawContext& ui_draw_context, float width, float height) {
  ImmediateDrawer::Begin(ui_draw_context, width, height);
  frame_ = std::make_shared<OverlayFrame>();
  frame_->width = coordinate_space_width();
  frame_->height = coordinate_space_height();
}

void OverlayDrawer::BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) {
  batch_first_vertex_ = uint32_t(frame_->vertices.size());
  batch_first_index_ = uint32_t(frame_->indices.size());
  frame_->vertices.insert(frame_->vertices.end(), batch.vertices,
                          batch.vertices + batch.vertex_count);
  if (batch.indices) {
    frame_->indices.insert(frame_->indices.end(), batch.indices, batch.indices + batch.index_count);
  } else {
    for (int i = 0; i < batch.vertex_count; ++i) frame_->indices.push_back(uint16_t(i));
  }
}

void OverlayDrawer::Draw(const rex::ui::ImmediateDraw& draw) {
  if (draw.primitive_type != rex::ui::ImmediatePrimitiveType::kTriangles || draw.count <= 0) {
    return;
  }
  OverlayFrame::Command command;
  command.first_index = batch_first_index_ + uint32_t(draw.index_offset);
  command.index_count = uint32_t(draw.count);
  command.base_vertex = int32_t(batch_first_vertex_) + draw.base_vertex;
  if (draw.texture) command.texture = static_cast<OverlayTexture*>(draw.texture)->data();
  if (draw.scissor) {
    command.clip[0] = draw.scissor_left;
    command.clip[1] = draw.scissor_top;
    command.clip[2] = draw.scissor_right;
    command.clip[3] = draw.scissor_bottom;
  } else {
    command.clip[2] = frame_->width;
    command.clip[3] = frame_->height;
  }
  frame_->commands.push_back(std::move(command));
}

void OverlayDrawer::End() {
  ImmediateDrawer::End();
  Publish(std::move(frame_));
}

void OverlayDrawer::PublishEmpty() { Publish(nullptr); }

struct OverlayRenderer::Impl {
  VulkanDevice* device = nullptr;
  bool failed = false;
  dl::TEXTURE_FORMAT format = dl::TEX_FORMAT_UNKNOWN;
  dl::RefCntAutoPtr<dl::IPipelineState> pso;
  dl::RefCntAutoPtr<dl::IBuffer> constants;
  dl::RefCntAutoPtr<dl::IBuffer> vertex_buffer, index_buffer;
  uint32_t vertex_capacity = 0, index_capacity = 0;
  dl::RefCntAutoPtr<dl::ISampler> linear, point;
  dl::RefCntAutoPtr<dl::ITexture> white;

  struct Uploaded {
    std::weak_ptr<const OverlayTextureData> source;
    dl::RefCntAutoPtr<dl::ITexture> texture;
    dl::RefCntAutoPtr<dl::IShaderResourceBinding> srb;
    uint64_t last_used = 0;
  };
  std::unordered_map<const OverlayTextureData*, Uploaded> textures;
  dl::RefCntAutoPtr<dl::IShaderResourceBinding> white_srb;
  uint64_t frame = 0;

  dl::IRenderDevice* render_device() const { return device->RenderDevice(); }

  bool CreatePipeline(dl::TEXTURE_FORMAT target) {
    dl::RefCntAutoPtr<dl::IShader> vs, ps;
    vs.Attach(device->CompileShader(kOverlayVS, dl::SHADER_TYPE_VERTEX, "OverlayVS"));
    ps.Attach(device->CompileShader(kOverlayPS, dl::SHADER_TYPE_PIXEL, "OverlayPS"));
    if (!vs || !ps) return false;
    dl::GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = "Overlay";
    ci.PSODesc.ResourceLayout.DefaultVariableType = dl::SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    dl::ShaderResourceVariableDesc vars[] = {
        {dl::SHADER_TYPE_VERTEX, "OverlayConstants", dl::SHADER_RESOURCE_VARIABLE_TYPE_STATIC},
    };
    ci.PSODesc.ResourceLayout.Variables = vars;
    ci.PSODesc.ResourceLayout.NumVariables = 1;
    ci.pVS = vs;
    ci.pPS = ps;
    dl::LayoutElement layout[] = {
        {0, 0, 2, dl::VT_FLOAT32, dl::False},
        {1, 0, 2, dl::VT_FLOAT32, dl::False},
        {2, 0, 4, dl::VT_UINT8, dl::True},
    };
    auto& gp = ci.GraphicsPipeline;
    gp.InputLayout.LayoutElements = layout;
    gp.InputLayout.NumElements = 3;
    gp.NumRenderTargets = 1;
    gp.RTVFormats[0] = target;
    gp.PrimitiveTopology = dl::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    gp.RasterizerDesc.CullMode = dl::CULL_MODE_NONE;
    gp.RasterizerDesc.ScissorEnable = dl::True;
    gp.DepthStencilDesc.DepthEnable = dl::False;
    auto& rt = gp.BlendDesc.RenderTargets[0];
    rt.BlendEnable = dl::True;
    rt.SrcBlend = dl::BLEND_FACTOR_SRC_ALPHA;
    rt.DestBlend = dl::BLEND_FACTOR_INV_SRC_ALPHA;
    rt.BlendOp = dl::BLEND_OPERATION_ADD;
    rt.SrcBlendAlpha = dl::BLEND_FACTOR_ONE;
    rt.DestBlendAlpha = dl::BLEND_FACTOR_INV_SRC_ALPHA;
    rt.BlendOpAlpha = dl::BLEND_OPERATION_ADD;
    pso.Release();
    render_device()->CreateGraphicsPipelineState(ci, &pso);
    if (!pso) return false;
    if (auto* var = pso->GetStaticVariableByName(dl::SHADER_TYPE_VERTEX, "OverlayConstants")) {
      var->Set(constants);
    }
    format = target;
    textures.clear();
    white_srb.Release();
    return true;
  }

  bool Init() {
    dl::BufferDesc cb;
    cb.Name = "OverlayConstants";
    cb.Size = 16;
    cb.Usage = dl::USAGE_DYNAMIC;
    cb.BindFlags = dl::BIND_UNIFORM_BUFFER;
    cb.CPUAccessFlags = dl::CPU_ACCESS_WRITE;
    render_device()->CreateBuffer(cb, nullptr, &constants);
    dl::SamplerDesc sampler;
    sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = dl::FILTER_TYPE_LINEAR;
    sampler.AddressU = sampler.AddressV = dl::TEXTURE_ADDRESS_CLAMP;
    render_device()->CreateSampler(sampler, &linear);
    sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = dl::FILTER_TYPE_POINT;
    render_device()->CreateSampler(sampler, &point);
    const uint32_t white_pixel = 0xFFFFFFFFu;
    white = CreateTexture("OverlayWhite", 1, 1, &white_pixel);
    return constants && linear && point && white;
  }

  dl::RefCntAutoPtr<dl::ITexture> CreateTexture(const char* name, uint32_t width, uint32_t height,
                                                const void* rgba) {
    dl::TextureDesc desc;
    desc.Name = name;
    desc.Type = dl::RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.Format = dl::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = dl::BIND_SHADER_RESOURCE;
    desc.Usage = dl::USAGE_IMMUTABLE;
    dl::TextureSubResData sub;
    sub.pData = rgba;
    sub.Stride = uint64_t(width) * 4;
    dl::TextureData init{&sub, 1};
    dl::RefCntAutoPtr<dl::ITexture> texture;
    render_device()->CreateTexture(desc, &init, &texture);
    return texture;
  }

  dl::RefCntAutoPtr<dl::IShaderResourceBinding> MakeBinding(dl::ITexture* texture,
                                                            bool linear_filter) {
    dl::RefCntAutoPtr<dl::IShaderResourceBinding> srb;
    pso->CreateShaderResourceBinding(&srb, true);
    if (!srb) return srb;
    if (auto* var = srb->GetVariableByName(dl::SHADER_TYPE_PIXEL, "g_Texture")) {
      var->Set(texture->GetDefaultView(dl::TEXTURE_VIEW_SHADER_RESOURCE));
    }
    if (auto* var = srb->GetVariableByName(dl::SHADER_TYPE_PIXEL, "g_Sampler")) {
      var->Set(linear_filter ? linear.RawPtr() : point.RawPtr());
    }
    return srb;
  }

  dl::IShaderResourceBinding* Binding(const std::shared_ptr<const OverlayTextureData>& data) {
    if (!data) {
      if (!white_srb) white_srb = MakeBinding(white, false);
      return white_srb;
    }
    auto it = textures.find(data.get());
    // A new texture can reuse a freed one's address.
    if (it != textures.end() && it->second.source.lock() != data) {
      textures.erase(it);
      it = textures.end();
    }
    if (it == textures.end()) {
      Uploaded uploaded;
      uploaded.source = data;
      uploaded.texture =
          CreateTexture("OverlayTexture", data->width, data->height, data->rgba.data());
      if (uploaded.texture) uploaded.srb = MakeBinding(uploaded.texture, data->linear);
      it = textures.emplace(data.get(), std::move(uploaded)).first;
    }
    it->second.last_used = frame;
    return it->second.srb;
  }

  bool Reserve(dl::RefCntAutoPtr<dl::IBuffer>& buffer, uint32_t& capacity, uint32_t needed,
               uint32_t stride, dl::BIND_FLAGS bind, const char* name) {
    if (buffer && capacity >= needed) return true;
    capacity = std::max(needed + needed / 2, 4096u);
    dl::BufferDesc desc;
    desc.Name = name;
    desc.Size = uint64_t(capacity) * stride;
    desc.Usage = dl::USAGE_DYNAMIC;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = dl::CPU_ACCESS_WRITE;
    buffer.Release();
    render_device()->CreateBuffer(desc, nullptr, &buffer);
    return buffer != nullptr;
  }
};

OverlayRenderer::OverlayRenderer(VulkanDevice* device) : impl_(std::make_unique<Impl>()) {
  impl_->device = device;
  if (!impl_->Init()) {
    impl_->failed = true;
    REXLOG_ERROR("Overlay: initialization failed, overlays disabled");
  }
}

OverlayRenderer::~OverlayRenderer() = default;

void OverlayRenderer::Draw(dl::IDeviceContext* context, dl::ITextureView* back_buffer) {
  Impl& m = *impl_;
  if (m.failed || !back_buffer) return;
  std::shared_ptr<const OverlayFrame> frame = Latest();
  if (!frame || frame->commands.empty() || frame->width <= 0.0f || frame->height <= 0.0f) return;

  const dl::TextureDesc& target = back_buffer->GetTexture()->GetDesc();
  if (m.format != target.Format && !m.CreatePipeline(target.Format)) {
    m.failed = true;
    REXLOG_ERROR("Overlay: pipeline creation failed, overlays disabled");
    return;
  }
  ++m.frame;

  const uint32_t vertex_count = uint32_t(frame->vertices.size());
  const uint32_t index_count = uint32_t(frame->indices.size());
  if (!m.Reserve(m.vertex_buffer, m.vertex_capacity, vertex_count, sizeof(rex::ui::ImmediateVertex),
                 dl::BIND_VERTEX_BUFFER, "OverlayVertices") ||
      !m.Reserve(m.index_buffer, m.index_capacity, index_count, sizeof(uint16_t),
                 dl::BIND_INDEX_BUFFER, "OverlayIndices")) {
    return;
  }
  {
    dl::MapHelper<rex::ui::ImmediateVertex> map(context, m.vertex_buffer, dl::MAP_WRITE,
                                                dl::MAP_FLAG_DISCARD);
    std::memcpy(map, frame->vertices.data(), vertex_count * sizeof(rex::ui::ImmediateVertex));
  }
  {
    dl::MapHelper<uint16_t> map(context, m.index_buffer, dl::MAP_WRITE, dl::MAP_FLAG_DISCARD);
    std::memcpy(map, frame->indices.data(), index_count * sizeof(uint16_t));
  }
  {
    // Coordinate space to clip space; Diligent's clip space is y-up.
    dl::MapHelper<float> map(context, m.constants, dl::MAP_WRITE, dl::MAP_FLAG_DISCARD);
    map[0] = 2.0f / frame->width;
    map[1] = -2.0f / frame->height;
    map[2] = -1.0f;
    map[3] = 1.0f;
  }

  context->SetRenderTargets(1, &back_buffer, nullptr,
                            dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
  dl::Viewport viewport;
  viewport.Width = float(target.Width);
  viewport.Height = float(target.Height);
  context->SetViewports(1, &viewport, target.Width, target.Height);
  context->SetPipelineState(m.pso);
  dl::IBuffer* vertex_buffers[] = {m.vertex_buffer};
  const uint64_t offsets[] = {0};
  context->SetVertexBuffers(0, 1, vertex_buffers, offsets,
                            dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                            dl::SET_VERTEX_BUFFERS_FLAG_RESET);
  context->SetIndexBuffer(m.index_buffer, 0, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

  const float sx = float(target.Width) / frame->width;
  const float sy = float(target.Height) / frame->height;
  dl::IShaderResourceBinding* bound = nullptr;
  for (const OverlayFrame::Command& command : frame->commands) {
    const int32_t x0 =
        std::clamp(int32_t(std::floor(command.clip[0] * sx)), 0, int32_t(target.Width));
    const int32_t y0 =
        std::clamp(int32_t(std::floor(command.clip[1] * sy)), 0, int32_t(target.Height));
    const int32_t x1 =
        std::clamp(int32_t(std::ceil(command.clip[2] * sx)), x0, int32_t(target.Width));
    const int32_t y1 =
        std::clamp(int32_t(std::ceil(command.clip[3] * sy)), y0, int32_t(target.Height));
    if (x1 <= x0 || y1 <= y0) continue;
    dl::IShaderResourceBinding* srb = m.Binding(command.texture);
    if (!srb) continue;
    if (srb != bound) {
      context->CommitShaderResources(srb, dl::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
      bound = srb;
    }
    dl::Rect scissor{x0, y0, x1, y1};
    context->SetScissorRects(1, &scissor, target.Width, target.Height);
    dl::DrawIndexedAttribs draw;
    draw.NumIndices = command.index_count;
    draw.FirstIndexLocation = command.first_index;
    draw.BaseVertex = uint32_t(command.base_vertex);
    draw.IndexType = dl::VT_UINT16;
    context->DrawIndexed(draw);
  }

  // Drop textures ImGui has released (icons of a closed overlay).
  if (m.frame % 120 == 0) {
    for (auto it = m.textures.begin(); it != m.textures.end();) {
      if (it->second.source.expired() || m.frame - it->second.last_used > 600) {
        it = m.textures.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void SetOverlayFrameRequest(std::function<void()> request) {
  std::lock_guard lock(g_request_mutex);
  g_request = std::move(request);
  if (!g_request) Publish(nullptr);
}

void RequestOverlayFrame() {
  std::lock_guard lock(g_request_mutex);
  if (g_request) g_request();
}

}  // namespace renderer
