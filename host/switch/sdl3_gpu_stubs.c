/* host/switch/sdl3_gpu_stubs.c -- written by host/switch/gen_gpu_stubs.py:
 * every SDL3 GPU function fails, so the game draws with OpenGL ES. */

#include <SDL3/SDL.h>

bool SDL_GPUSupportsShaderFormats(SDL_GPUShaderFormat format_flags, const char *name)
{
    (void)format_flags;
    (void)name;
    return false;
}

bool SDL_GPUSupportsProperties(SDL_PropertiesID props)
{
    (void)props;
    return false;
}

SDL_GPUDevice * SDL_CreateGPUDevice(SDL_GPUShaderFormat format_flags, bool debug_mode, const char *name)
{
    (void)format_flags;
    (void)debug_mode;
    (void)name;
    return NULL;
}

SDL_GPUDevice * SDL_CreateGPUDeviceWithProperties(SDL_PropertiesID props)
{
    (void)props;
    return NULL;
}

void SDL_DestroyGPUDevice(SDL_GPUDevice *device)
{
    (void)device;
}

int SDL_GetNumGPUDrivers(void)
{
    return (int)0;
}

const char * SDL_GetGPUDriver(int index)
{
    (void)index;
    return NULL;
}

const char * SDL_GetGPUDeviceDriver(SDL_GPUDevice *device)
{
    (void)device;
    return NULL;
}

SDL_GPUShaderFormat SDL_GetGPUShaderFormats(SDL_GPUDevice *device)
{
    (void)device;
    return (SDL_GPUShaderFormat)0;
}

SDL_PropertiesID SDL_GetGPUDeviceProperties(SDL_GPUDevice *device)
{
    (void)device;
    return (SDL_PropertiesID)0;
}

SDL_GPUComputePipeline * SDL_CreateGPUComputePipeline(SDL_GPUDevice *device, const SDL_GPUComputePipelineCreateInfo *createinfo)
{
    (void)device;
    (void)createinfo;
    return NULL;
}

SDL_GPUGraphicsPipeline * SDL_CreateGPUGraphicsPipeline(SDL_GPUDevice *device, const SDL_GPUGraphicsPipelineCreateInfo *createinfo)
{
    (void)device;
    (void)createinfo;
    return NULL;
}

SDL_GPUSampler * SDL_CreateGPUSampler(SDL_GPUDevice *device, const SDL_GPUSamplerCreateInfo *createinfo)
{
    (void)device;
    (void)createinfo;
    return NULL;
}

SDL_GPUShader * SDL_CreateGPUShader(SDL_GPUDevice *device, const SDL_GPUShaderCreateInfo *createinfo)
{
    (void)device;
    (void)createinfo;
    return NULL;
}

SDL_GPUTexture * SDL_CreateGPUTexture(SDL_GPUDevice *device, const SDL_GPUTextureCreateInfo *createinfo)
{
    (void)device;
    (void)createinfo;
    return NULL;
}

SDL_GPUBuffer * SDL_CreateGPUBuffer(SDL_GPUDevice *device, const SDL_GPUBufferCreateInfo *createinfo)
{
    (void)device;
    (void)createinfo;
    return NULL;
}

SDL_GPUTransferBuffer * SDL_CreateGPUTransferBuffer(SDL_GPUDevice *device, const SDL_GPUTransferBufferCreateInfo *createinfo)
{
    (void)device;
    (void)createinfo;
    return NULL;
}

void SDL_SetGPUBufferName(SDL_GPUDevice *device, SDL_GPUBuffer *buffer, const char *text)
{
    (void)device;
    (void)buffer;
    (void)text;
}

void SDL_SetGPUTextureName(SDL_GPUDevice *device, SDL_GPUTexture *texture, const char *text)
{
    (void)device;
    (void)texture;
    (void)text;
}

void SDL_InsertGPUDebugLabel(SDL_GPUCommandBuffer *command_buffer, const char *text)
{
    (void)command_buffer;
    (void)text;
}

void SDL_PushGPUDebugGroup(SDL_GPUCommandBuffer *command_buffer, const char *name)
{
    (void)command_buffer;
    (void)name;
}

void SDL_PopGPUDebugGroup(SDL_GPUCommandBuffer *command_buffer)
{
    (void)command_buffer;
}

void SDL_ReleaseGPUTexture(SDL_GPUDevice *device, SDL_GPUTexture *texture)
{
    (void)device;
    (void)texture;
}

void SDL_ReleaseGPUSampler(SDL_GPUDevice *device, SDL_GPUSampler *sampler)
{
    (void)device;
    (void)sampler;
}

void SDL_ReleaseGPUBuffer(SDL_GPUDevice *device, SDL_GPUBuffer *buffer)
{
    (void)device;
    (void)buffer;
}

void SDL_ReleaseGPUTransferBuffer(SDL_GPUDevice *device, SDL_GPUTransferBuffer *transfer_buffer)
{
    (void)device;
    (void)transfer_buffer;
}

void SDL_ReleaseGPUComputePipeline(SDL_GPUDevice *device, SDL_GPUComputePipeline *compute_pipeline)
{
    (void)device;
    (void)compute_pipeline;
}

void SDL_ReleaseGPUShader(SDL_GPUDevice *device, SDL_GPUShader *shader)
{
    (void)device;
    (void)shader;
}

void SDL_ReleaseGPUGraphicsPipeline(SDL_GPUDevice *device, SDL_GPUGraphicsPipeline *graphics_pipeline)
{
    (void)device;
    (void)graphics_pipeline;
}

SDL_GPUCommandBuffer * SDL_AcquireGPUCommandBuffer(SDL_GPUDevice *device)
{
    (void)device;
    return NULL;
}

void SDL_PushGPUVertexUniformData(SDL_GPUCommandBuffer *command_buffer, Uint32 slot_index, const void *data, Uint32 length)
{
    (void)command_buffer;
    (void)slot_index;
    (void)data;
    (void)length;
}

void SDL_PushGPUFragmentUniformData(SDL_GPUCommandBuffer *command_buffer, Uint32 slot_index, const void *data, Uint32 length)
{
    (void)command_buffer;
    (void)slot_index;
    (void)data;
    (void)length;
}

void SDL_PushGPUComputeUniformData(SDL_GPUCommandBuffer *command_buffer, Uint32 slot_index, const void *data, Uint32 length)
{
    (void)command_buffer;
    (void)slot_index;
    (void)data;
    (void)length;
}

SDL_GPURenderPass * SDL_BeginGPURenderPass(SDL_GPUCommandBuffer *command_buffer, const SDL_GPUColorTargetInfo *color_target_infos, Uint32 num_color_targets, const SDL_GPUDepthStencilTargetInfo *depth_stencil_target_info)
{
    (void)command_buffer;
    (void)color_target_infos;
    (void)num_color_targets;
    (void)depth_stencil_target_info;
    return NULL;
}

void SDL_BindGPUGraphicsPipeline(SDL_GPURenderPass *render_pass, SDL_GPUGraphicsPipeline *graphics_pipeline)
{
    (void)render_pass;
    (void)graphics_pipeline;
}

void SDL_SetGPUViewport(SDL_GPURenderPass *render_pass, const SDL_GPUViewport *viewport)
{
    (void)render_pass;
    (void)viewport;
}

void SDL_SetGPUScissor(SDL_GPURenderPass *render_pass, const SDL_Rect *scissor)
{
    (void)render_pass;
    (void)scissor;
}

void SDL_SetGPUBlendConstants(SDL_GPURenderPass *render_pass, SDL_FColor blend_constants)
{
    (void)render_pass;
    (void)blend_constants;
}

void SDL_SetGPUStencilReference(SDL_GPURenderPass *render_pass, Uint8 reference)
{
    (void)render_pass;
    (void)reference;
}

void SDL_BindGPUVertexBuffers(SDL_GPURenderPass *render_pass, Uint32 first_slot, const SDL_GPUBufferBinding *bindings, Uint32 num_bindings)
{
    (void)render_pass;
    (void)first_slot;
    (void)bindings;
    (void)num_bindings;
}

void SDL_BindGPUIndexBuffer(SDL_GPURenderPass *render_pass, const SDL_GPUBufferBinding *binding, SDL_GPUIndexElementSize index_element_size)
{
    (void)render_pass;
    (void)binding;
    (void)index_element_size;
}

void SDL_BindGPUVertexSamplers(SDL_GPURenderPass *render_pass, Uint32 first_slot, const SDL_GPUTextureSamplerBinding *texture_sampler_bindings, Uint32 num_bindings)
{
    (void)render_pass;
    (void)first_slot;
    (void)texture_sampler_bindings;
    (void)num_bindings;
}

void SDL_BindGPUVertexStorageTextures(SDL_GPURenderPass *render_pass, Uint32 first_slot, SDL_GPUTexture *const *storage_textures, Uint32 num_bindings)
{
    (void)render_pass;
    (void)first_slot;
    (void)storage_textures;
    (void)num_bindings;
}

void SDL_BindGPUVertexStorageBuffers(SDL_GPURenderPass *render_pass, Uint32 first_slot, SDL_GPUBuffer *const *storage_buffers, Uint32 num_bindings)
{
    (void)render_pass;
    (void)first_slot;
    (void)storage_buffers;
    (void)num_bindings;
}

void SDL_BindGPUFragmentSamplers(SDL_GPURenderPass *render_pass, Uint32 first_slot, const SDL_GPUTextureSamplerBinding *texture_sampler_bindings, Uint32 num_bindings)
{
    (void)render_pass;
    (void)first_slot;
    (void)texture_sampler_bindings;
    (void)num_bindings;
}

void SDL_BindGPUFragmentStorageTextures(SDL_GPURenderPass *render_pass, Uint32 first_slot, SDL_GPUTexture *const *storage_textures, Uint32 num_bindings)
{
    (void)render_pass;
    (void)first_slot;
    (void)storage_textures;
    (void)num_bindings;
}

void SDL_BindGPUFragmentStorageBuffers(SDL_GPURenderPass *render_pass, Uint32 first_slot, SDL_GPUBuffer *const *storage_buffers, Uint32 num_bindings)
{
    (void)render_pass;
    (void)first_slot;
    (void)storage_buffers;
    (void)num_bindings;
}

void SDL_DrawGPUIndexedPrimitives(SDL_GPURenderPass *render_pass, Uint32 num_indices, Uint32 num_instances, Uint32 first_index, Sint32 vertex_offset, Uint32 first_instance)
{
    (void)render_pass;
    (void)num_indices;
    (void)num_instances;
    (void)first_index;
    (void)vertex_offset;
    (void)first_instance;
}

void SDL_DrawGPUPrimitives(SDL_GPURenderPass *render_pass, Uint32 num_vertices, Uint32 num_instances, Uint32 first_vertex, Uint32 first_instance)
{
    (void)render_pass;
    (void)num_vertices;
    (void)num_instances;
    (void)first_vertex;
    (void)first_instance;
}

void SDL_DrawGPUPrimitivesIndirect(SDL_GPURenderPass *render_pass, SDL_GPUBuffer *buffer, Uint32 offset, Uint32 draw_count)
{
    (void)render_pass;
    (void)buffer;
    (void)offset;
    (void)draw_count;
}

void SDL_DrawGPUIndexedPrimitivesIndirect(SDL_GPURenderPass *render_pass, SDL_GPUBuffer *buffer, Uint32 offset, Uint32 draw_count)
{
    (void)render_pass;
    (void)buffer;
    (void)offset;
    (void)draw_count;
}

void SDL_EndGPURenderPass(SDL_GPURenderPass *render_pass)
{
    (void)render_pass;
}

SDL_GPUComputePass * SDL_BeginGPUComputePass(SDL_GPUCommandBuffer *command_buffer, const SDL_GPUStorageTextureReadWriteBinding *storage_texture_bindings, Uint32 num_storage_texture_bindings, const SDL_GPUStorageBufferReadWriteBinding *storage_buffer_bindings, Uint32 num_storage_buffer_bindings)
{
    (void)command_buffer;
    (void)storage_texture_bindings;
    (void)num_storage_texture_bindings;
    (void)storage_buffer_bindings;
    (void)num_storage_buffer_bindings;
    return NULL;
}

void SDL_BindGPUComputePipeline(SDL_GPUComputePass *compute_pass, SDL_GPUComputePipeline *compute_pipeline)
{
    (void)compute_pass;
    (void)compute_pipeline;
}

void SDL_BindGPUComputeSamplers(SDL_GPUComputePass *compute_pass, Uint32 first_slot, const SDL_GPUTextureSamplerBinding *texture_sampler_bindings, Uint32 num_bindings)
{
    (void)compute_pass;
    (void)first_slot;
    (void)texture_sampler_bindings;
    (void)num_bindings;
}

void SDL_BindGPUComputeStorageTextures(SDL_GPUComputePass *compute_pass, Uint32 first_slot, SDL_GPUTexture *const *storage_textures, Uint32 num_bindings)
{
    (void)compute_pass;
    (void)first_slot;
    (void)storage_textures;
    (void)num_bindings;
}

void SDL_BindGPUComputeStorageBuffers(SDL_GPUComputePass *compute_pass, Uint32 first_slot, SDL_GPUBuffer *const *storage_buffers, Uint32 num_bindings)
{
    (void)compute_pass;
    (void)first_slot;
    (void)storage_buffers;
    (void)num_bindings;
}

void SDL_DispatchGPUCompute(SDL_GPUComputePass *compute_pass, Uint32 groupcount_x, Uint32 groupcount_y, Uint32 groupcount_z)
{
    (void)compute_pass;
    (void)groupcount_x;
    (void)groupcount_y;
    (void)groupcount_z;
}

void SDL_DispatchGPUComputeIndirect(SDL_GPUComputePass *compute_pass, SDL_GPUBuffer *buffer, Uint32 offset)
{
    (void)compute_pass;
    (void)buffer;
    (void)offset;
}

void SDL_EndGPUComputePass(SDL_GPUComputePass *compute_pass)
{
    (void)compute_pass;
}

void * SDL_MapGPUTransferBuffer(SDL_GPUDevice *device, SDL_GPUTransferBuffer *transfer_buffer, bool cycle)
{
    (void)device;
    (void)transfer_buffer;
    (void)cycle;
    return NULL;
}

void SDL_UnmapGPUTransferBuffer(SDL_GPUDevice *device, SDL_GPUTransferBuffer *transfer_buffer)
{
    (void)device;
    (void)transfer_buffer;
}

SDL_GPUCopyPass * SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer *command_buffer)
{
    (void)command_buffer;
    return NULL;
}

void SDL_UploadToGPUTexture(SDL_GPUCopyPass *copy_pass, const SDL_GPUTextureTransferInfo *source, const SDL_GPUTextureRegion *destination, bool cycle)
{
    (void)copy_pass;
    (void)source;
    (void)destination;
    (void)cycle;
}

void SDL_UploadToGPUBuffer(SDL_GPUCopyPass *copy_pass, const SDL_GPUTransferBufferLocation *source, const SDL_GPUBufferRegion *destination, bool cycle)
{
    (void)copy_pass;
    (void)source;
    (void)destination;
    (void)cycle;
}

void SDL_CopyGPUTextureToTexture(SDL_GPUCopyPass *copy_pass, const SDL_GPUTextureLocation *source, const SDL_GPUTextureLocation *destination, Uint32 w, Uint32 h, Uint32 d, bool cycle)
{
    (void)copy_pass;
    (void)source;
    (void)destination;
    (void)w;
    (void)h;
    (void)d;
    (void)cycle;
}

void SDL_CopyGPUBufferToBuffer(SDL_GPUCopyPass *copy_pass, const SDL_GPUBufferLocation *source, const SDL_GPUBufferLocation *destination, Uint32 size, bool cycle)
{
    (void)copy_pass;
    (void)source;
    (void)destination;
    (void)size;
    (void)cycle;
}

void SDL_DownloadFromGPUTexture(SDL_GPUCopyPass *copy_pass, const SDL_GPUTextureRegion *source, const SDL_GPUTextureTransferInfo *destination)
{
    (void)copy_pass;
    (void)source;
    (void)destination;
}

void SDL_DownloadFromGPUBuffer(SDL_GPUCopyPass *copy_pass, const SDL_GPUBufferRegion *source, const SDL_GPUTransferBufferLocation *destination)
{
    (void)copy_pass;
    (void)source;
    (void)destination;
}

void SDL_EndGPUCopyPass(SDL_GPUCopyPass *copy_pass)
{
    (void)copy_pass;
}

void SDL_GenerateMipmapsForGPUTexture(SDL_GPUCommandBuffer *command_buffer, SDL_GPUTexture *texture)
{
    (void)command_buffer;
    (void)texture;
}

void SDL_BlitGPUTexture(SDL_GPUCommandBuffer *command_buffer, const SDL_GPUBlitInfo *info)
{
    (void)command_buffer;
    (void)info;
}

bool SDL_WindowSupportsGPUSwapchainComposition(SDL_GPUDevice *device, SDL_Window *window, SDL_GPUSwapchainComposition swapchain_composition)
{
    (void)device;
    (void)window;
    (void)swapchain_composition;
    return false;
}

bool SDL_WindowSupportsGPUPresentMode(SDL_GPUDevice *device, SDL_Window *window, SDL_GPUPresentMode present_mode)
{
    (void)device;
    (void)window;
    (void)present_mode;
    return false;
}

bool SDL_ClaimWindowForGPUDevice(SDL_GPUDevice *device, SDL_Window *window)
{
    (void)device;
    (void)window;
    return false;
}

void SDL_ReleaseWindowFromGPUDevice(SDL_GPUDevice *device, SDL_Window *window)
{
    (void)device;
    (void)window;
}

bool SDL_SetGPUSwapchainParameters(SDL_GPUDevice *device, SDL_Window *window, SDL_GPUSwapchainComposition swapchain_composition, SDL_GPUPresentMode present_mode)
{
    (void)device;
    (void)window;
    (void)swapchain_composition;
    (void)present_mode;
    return false;
}

bool SDL_SetGPUAllowedFramesInFlight(SDL_GPUDevice *device, Uint32 allowed_frames_in_flight)
{
    (void)device;
    (void)allowed_frames_in_flight;
    return false;
}

SDL_GPUTextureFormat SDL_GetGPUSwapchainTextureFormat(SDL_GPUDevice *device, SDL_Window *window)
{
    (void)device;
    (void)window;
    return (SDL_GPUTextureFormat)0;
}

bool SDL_AcquireGPUSwapchainTexture(SDL_GPUCommandBuffer *command_buffer, SDL_Window *window, SDL_GPUTexture **swapchain_texture, Uint32 *swapchain_texture_width, Uint32 *swapchain_texture_height)
{
    (void)command_buffer;
    (void)window;
    (void)swapchain_texture;
    (void)swapchain_texture_width;
    (void)swapchain_texture_height;
    return false;
}

bool SDL_WaitForGPUSwapchain(SDL_GPUDevice *device, SDL_Window *window)
{
    (void)device;
    (void)window;
    return false;
}

bool SDL_WaitAndAcquireGPUSwapchainTexture(SDL_GPUCommandBuffer *command_buffer, SDL_Window *window, SDL_GPUTexture **swapchain_texture, Uint32 *swapchain_texture_width, Uint32 *swapchain_texture_height)
{
    (void)command_buffer;
    (void)window;
    (void)swapchain_texture;
    (void)swapchain_texture_width;
    (void)swapchain_texture_height;
    return false;
}

bool SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *command_buffer)
{
    (void)command_buffer;
    return false;
}

SDL_GPUFence * SDL_SubmitGPUCommandBufferAndAcquireFence(SDL_GPUCommandBuffer *command_buffer)
{
    (void)command_buffer;
    return NULL;
}

bool SDL_CancelGPUCommandBuffer(SDL_GPUCommandBuffer *command_buffer)
{
    (void)command_buffer;
    return false;
}

bool SDL_WaitForGPUIdle(SDL_GPUDevice *device)
{
    (void)device;
    return false;
}

bool SDL_WaitForGPUFences(SDL_GPUDevice *device, bool wait_all, SDL_GPUFence *const *fences, Uint32 num_fences)
{
    (void)device;
    (void)wait_all;
    (void)fences;
    (void)num_fences;
    return false;
}

bool SDL_QueryGPUFence(SDL_GPUDevice *device, SDL_GPUFence *fence)
{
    (void)device;
    (void)fence;
    return false;
}

void SDL_ReleaseGPUFence(SDL_GPUDevice *device, SDL_GPUFence *fence)
{
    (void)device;
    (void)fence;
}

Uint32 SDL_GPUTextureFormatTexelBlockSize(SDL_GPUTextureFormat format)
{
    (void)format;
    return (Uint32)0;
}

bool SDL_GPUTextureSupportsFormat(SDL_GPUDevice *device, SDL_GPUTextureFormat format, SDL_GPUTextureType type, SDL_GPUTextureUsageFlags usage)
{
    (void)device;
    (void)format;
    (void)type;
    (void)usage;
    return false;
}

bool SDL_GPUTextureSupportsSampleCount(SDL_GPUDevice *device, SDL_GPUTextureFormat format, SDL_GPUSampleCount sample_count)
{
    (void)device;
    (void)format;
    (void)sample_count;
    return false;
}

Uint32 SDL_CalculateGPUTextureFormatSize(SDL_GPUTextureFormat format, Uint32 width, Uint32 height, Uint32 depth_or_layer_count)
{
    (void)format;
    (void)width;
    (void)height;
    (void)depth_or_layer_count;
    return (Uint32)0;
}

SDL_PixelFormat SDL_GetPixelFormatFromGPUTextureFormat(SDL_GPUTextureFormat format)
{
    (void)format;
    return (SDL_PixelFormat)0;
}

SDL_GPUTextureFormat SDL_GetGPUTextureFormatFromPixelFormat(SDL_PixelFormat format)
{
    (void)format;
    return (SDL_GPUTextureFormat)0;
}

void SDL_GDKSuspendGPU(SDL_GPUDevice *device)
{
    (void)device;
}

void SDL_GDKResumeGPU(SDL_GPUDevice *device)
{
    (void)device;
}
