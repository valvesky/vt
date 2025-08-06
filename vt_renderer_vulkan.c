#pragma once

#include "vt_renderer.h"

#ifndef VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#define VK_EXT_DEBUG_REPORT_EXTENSION_NAME "VK_EXT_debug_report"
#endif

struct Renderer {
  SDL_Window *window;
  uint64_t frames;
  uint32_t width;
  uint32_t height;
};

typedef struct Vulkan_Context Vulkan_Context;

struct Vulkan_Context {
  VkAllocationCallbacks *allocator;
  VkInstance instance;
  VkPhysicalDevice physical_device;
};

static Vulkan_Context vk_context; 

char *slurp_file(const char * const src, size_t*);

bool renderer_create(Renderer* r) {
  Renderer r_zero = {0};
  assert(memcmp(r, &r_zero, sizeof(*r))==0);

  r->width = 800;
  r->height = 600;
  
  SDL_Init(SDL_INIT_VIDEO);
  r->window = SDL_CreateWindow( "vt", r->height, r->height, SDL_WINDOW_VULKAN );

  uint32_t count_instance_extensions  = 0;
  const char * const *instance_extensions = SDL_Vulkan_GetInstanceExtensions(&count_instance_extensions);
  if (!instance_extensions ) {
    SDL_Quit();
    return false;
  }

  int count_extensions = count_instance_extensions + 1;
  const char **extensions = SDL_malloc(count_extensions * sizeof(const char *));
  if (!extensions) {
    SDL_free(extensions);
    SDL_Quit();
    return false;
  }
  extensions[0] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
  SDL_memcpy(&extensions[1], instance_extensions, count_instance_extensions * sizeof(const char*)); 

  VkApplicationInfo app_info = {
    .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pApplicationName = "vt",
    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    .pEngineName = "vt_engine",
    .engineVersion = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion = VK_API_VERSION_1_0,
  };

  VkInstanceCreateInfo create_info = {0}; // zero
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledExtensionCount = count_extensions;
  create_info.ppEnabledExtensionNames = extensions;

  vk_context.allocator = 0;
  VkResult result = vkCreateInstance(&create_info, vk_context.allocator, &vk_context.instance);

  SDL_free(extensions);

  if (result != VK_SUCCESS) {
    SDL_Quit();
    return false;
  }

  return true;
}

/* --- Pipeline --- */
void renderer_destroy(Renderer *r) {
  vkDestroyInstance(vk_context.instance, NULL);
  SDL_DestroyWindow(r->window);
  SDL_Quit();
}

char *slurp_file(const char * const src, size_t *size) {
  int fd = open(src, O_RDONLY, 0644);
  if (fd < 0) return NULL;

  size_t len = lseek(fd, 0, SEEK_END);
  *size = len; 
  lseek(fd, 0, SEEK_SET);

  char *retv = (char*) malloc(len+1);
  read(fd, retv, len);
  retv[len] = '\0';
  return retv;
}

bool vk_load_shader_module(VkDevice device, const char* path, VkShaderModule* out_module) {

  size_t size;
  char* buffer = slurp_file(path, &size);

  VkShaderModuleCreateInfo create_info = {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = size,
    .pCode = (uint32_t*) buffer,
  };

  VkResult result = vkCreateShaderModule(device, &create_info, NULL, out_module);
  free(buffer);

  if (result != VK_SUCCESS) {
    fprintf(stderr, "ERROR: Failed to create shader module: %d\n", result);
    return false;
  }

  return true;
}

bool vk_create_graphics_pipeline(const char* vert_path, const char* frag_path) {
  size_t  vert_file_size = 0;
  size_t  frag_file_size = 0;
  char* vert_file_buf = slurp_file(vert_path, &vert_file_size);
  char* frag_file_buf = slurp_file(frag_path, &frag_file_size);

  // VkResult vkCreateGraphicsPipelines(
  //     VkDevice                                    device,
  //     VkPipelineCache                             pipelineCache,
  //     uint32_t                                    createInfoCount,
  //     const VkGraphicsPipelineCreateInfo*         pCreateInfos,
  //     const VkAllocationCallbacks*                pAllocator,
  //     VkPipeline*                                 pPipelines);
  //
  free(frag_file_buf);
  free(vert_file_buf);
  return true;
}
