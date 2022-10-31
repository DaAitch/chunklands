
# Fix inflight frames uniform buffer - 2022-10-29

As you may have noticed, [some Chunklands videos on twitter](https://twitter.com/chunklands/status/1585697803124211761?s=20&t=ZOc5DEi0bIiHoOHHi7jw-A) have strange artifacts that blocks or chunks aren't moved according to the rest of the world which result in small gaps. Such behavior indicates improper data synchronization.

As long as the player moves or looks around the MVP (model, view, project) matrices are updated. In our case we store them in host-visible, cache-coherent uniform buffers. Whenever updating uniform buffer memory on the host and drawing the last frame overlaps we may get such unwanted visual effects.

For host-visible memory it's important to make updates when no further reads by the graphics card are queued. That situation is typically right after a fence indicating that the frame has been processed. Here we only have one shared uniform buffer for all frames which makes inflight frames obsolete if we would sychronize them.

The solution is to duplicate uniform buffers for each frame that after waiting for the fence, it's guaranteed that the uniform buffer for that frame index can be updated. Let's go through all changes top-down:

1/ Vertex and fragment shader binding doesn't change. It always just uses `ubo` with a specific binding:

```glsl
layout(binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 start_selection;
    vec4 end_selection;
} ubo;
```

2/ Game loop fence synchronization has to be before calculating and updating matrices.

```rust
let frame = presentation.sync_next_frame(&self.device)?; // fence wait
// ..
let ubo = UniformBufferObject {
    model,
    view,
    proj,
    start_selection,
    end_selection,
};

self.block_graphics_pipeline_descriptors.uniform_buffer[frame].write(ubo);
```

3/ Feeding the shaders is done with Vulkan descriptors. If we need to change bindings for each frame, we need to also duplicate descriptor sets. The descriptor set layout of course also doesn't change.

```rust
let layouts = vec![descriptor_set_layout; INFLIGHT_FRAME_COUNT];
let handles = unsafe {
    device.allocate_descriptors_sets(
        &DescriptorSetAllocateInfo::default()
            .descriptor_pool(&descriptor_pool)
            .set_layouts(layouts.as_slice()),
    )?
};

for (i, handle) in handles.iter().enumerate() {
    unsafe {
        device.update_descriptor_sets(
            &[
                WriteDescriptorSet::default()
                    .dst_set(&handle)
                    .dst_binding(0)
                    .descriptor_type(vk::DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    .buffer_info(&[DescriptorBufferInfo::default()
                        .buffer(uniform_buffer.buffer())
                        .range(uniform_buffer.layout().stride() as u64)
                        .offset((i * uniform_buffer.layout().stride()) as u64)]),
                WriteDescriptorSet::default()
                    .dst_set(&handle)
                    .dst_binding(1)
                    .descriptor_type(vk::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
                    .image_info(&[DescriptorImageInfo::default()
                        .image_layout(vk::IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                        .image_view(texture_image_view)
                        .sampler(texture_sampler)]),
            ],
            &[],
        );
    }
}
```

Take care that descriptors like uniform buffers (and others) need proper memory alignment for offsets. Read `physical_device_properties.limits.minUniformBufferOffsetAlignment` and allocate enough memory that an array of uniforms can be stored. Our uniform needs a size of `size_of::<UniformBufferObject>() == 0xE0`, but `minUniformBufferOffsetAlignment == 0x100`. As buffer offset alignment is a dynamic value, we cannot align our type at compile-time.

An easier alternative than playing around with alignment is to create separate buffers and bind separate memory for uniforms, using `offset = 0`, but it's more convenient to use less buffers and larger memory chunks. Increasing the inflight frames count, we actually don't need to create new buffers, but make device memory larger and bind our sets.

I also want to mention, that of course I stumbled into the alignment trap of uniform buffers, but in release mode without validation layer, everything just worked. At the final debug mode check, that also everything is destroyed properly, I got validation errors, which also make the Vulkan API return errors.

4/ To be allowed to allocate enough sets, the descriptor pool has to be updated.

```rust
fn create_descriptor_pool(device: &Device) -> Result<DescriptorPool> {
    unsafe {
        device.create_descriptor_pool(
            &DescriptorPoolCreateInfo::default()
                .max_sets(INFLIGHT_FRAME_COUNT as u32)
                .pool_sizes(&[
                    vk::DescriptorPoolSize {
                        ty: vk::DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        descriptorCount: INFLIGHT_FRAME_COUNT as u32,
                    },
                    vk::DescriptorPoolSize {
                        ty: vk::DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        descriptorCount: INFLIGHT_FRAME_COUNT as u32,
                    },
                ]),
        )
    }
}
```

Again validation layer will report an error and return a error value. Without validation layer, it works.


That pretty much is it. As a rule of thumb, it's mostly a good idea that every inflight frame use a separate descriptor set which is bound to exclusive device memory. There might be reasons against duplication, like our texture sampler doesn't change and there are other mechanisms like push constants that can be updated per frame via command-buffer command.