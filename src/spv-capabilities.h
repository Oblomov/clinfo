static const cl_uint2 spirv_capability_ranges[] = {
	{{ 0, 71 }},
	{{ 4165, 4168 }},
	{{ 4174, 4176 }},
	{{ 4191, 4191 }},
	{{ 4201, 4201 }},
	{{ 4212, 4213 }},
	{{ 4228, 4232 }},
	{{ 4422, 4423 }},
	{{ 4427, 4450 }},
	{{ 4464, 4473 }},
	{{ 4478, 4479 }},
	{{ 4484, 4486 }},
	{{ 4495, 4498 }},
	{{ 4504, 4506 }},
	{{ 4539, 4539 }},
	{{ 4543, 4544 }},
	{{ 5008, 5016 }},
	{{ 5055, 5055 }},
	{{ 5067, 5067 }},
	{{ 5087, 5087 }},
	{{ 5112, 5120 }},
	{{ 5128, 5128 }},
	{{ 5146, 5146 }},
	{{ 5156, 5156 }},
	{{ 5181, 5181 }},
	{{ 5249, 5255 }},
	{{ 5259, 5260 }},
	{{ 5265, 5266 }},
	{{ 5282, 5284 }},
	{{ 5288, 5288 }},
	{{ 5291, 5291 }},
	{{ 5297, 5297 }},
	{{ 5301, 5312 }},
	{{ 5336, 5336 }},
	{{ 5340, 5341 }},
	{{ 5345, 5347 }},
	{{ 5350, 5350 }},
	{{ 5353, 5353 }},
        {{ 5357, 5357 }},
        {{ 5363, 5363 }},
        {{ 5372, 5373 }},
        {{ 5378, 5383 }},
        {{ 5388, 5394 }},
        {{ 5404, 5404 }},
        {{ 5409, 5409 }},
        {{ 5414, 5414 }},
        {{ 5418, 5419 }},
        {{ 5423, 5439 }},
        {{ 5447, 5447 }},
        {{ 5568, 5570 }},
        {{ 5579, 5584 }},
        {{ 5603, 5606 }},
        {{ 5612, 5619 }},
        {{ 5629, 5629 }},
        {{ 5696, 5698 }},
        {{ 5817, 5817 }},
        {{ 5821, 5824 }},
        {{ 5837, 5837 }},
        {{ 5844, 5845 }},
        {{ 5886, 5888 }},
        {{ 5892, 5892 }},
        {{ 5897, 5898 }},
        {{ 5904, 5910 }},
        {{ 5916, 5916 }},
        {{ 5920, 5922 }},
        {{ 5935, 5935 }},
        {{ 5939, 5939 }},
        {{ 5943, 5948 }},
        {{ 6016, 6034 }},
        {{ 6089, 6089 }},
        {{ 6094, 6095 }},
        {{ 6114, 6115 }},
        {{ 6141, 6141 }},
        {{ 6144, 6144 }},
        {{ 6150, 6150 }},
        {{ 6161, 6162 }},
        {{ 6169, 6174 }},
        {{ 6185, 6189 }},
        {{ 6207, 6207 }},
        {{ 6220, 6220 }},
        {{ 6228, 6230 }},
        {{ 6236, 6236 }},
        {{ 6241, 6246 }},
        {{ 6257, 6257 }},
        {{ 6265, 6265 }},
        {{ 6400, 6400 }},
        {{ 6425, 6427 }},
        {{ 6441, 6441 }},
        {{ 6460, 6460 }},
        {{ 6528, 6528 }},
        {{ 6912, 6915 }},
        {{ 7041, 7041 }},
};

const size_t num_spirv_caps_ranges = ARRAY_SIZE(spirv_capability_ranges);

static const char* spirv_capabilities[] = {
        "Matrix", /* (0), */
        "Shader", /* (1), */
        "Geometry", /* (2), */
        "Tessellation", /* (3), */
        "Addresses", /* (4), */
        "Linkage", /* (5), */
        "Kernel", /* (6), */
        "Vector16", /* (7), */
        "Float16Buffer", /* (8), */
        "Float16", /* (9), */
        "Float64", /* (10), */
        "Int64", /* (11), */
        "Int64Atomics", /* (12), */
        "ImageBasic", /* (13), */
        "ImageReadWrite", /* (14), */
        "ImageMipmap", /* (15), */
	"<unknown>", /* (16), */
        "Pipes", /* (17), */
        "Groups", /* (18), */
        "DeviceEnqueue", /* (19), */
        "LiteralSampler", /* (20), */
        "AtomicStorage", /* (21), */
        "Int16", /* (22), */
        "TessellationPointSize", /* (23), */
        "GeometryPointSize", /* (24), */
        "ImageGatherExtended", /* (25), */
        "<unknown>", /* (26), */
        "StorageImageMultisample", /* (27), */
        "UniformBufferArrayDynamicIndexing", /* (28), */
        "SampledImageArrayDynamicIndexing", /* (29), */
        "StorageBufferArrayDynamicIndexing", /* (30), */
        "StorageImageArrayDynamicIndexing", /* (31), */
        "ClipDistance", /* (32), */
        "CullDistance", /* (33), */
        "ImageCubeArray", /* (34), */
        "SampleRateShading", /* (35), */
        "ImageRect", /* (36), */
        "SampledRect", /* (37), */
        "GenericPointer", /* (38), */
        "Int8", /* (39), */
        "InputAttachment", /* (40), */
        "SparseResidency", /* (41), */
        "MinLod", /* (42), */
        "Sampled1D", /* (43), */
        "Image1D", /* (44), */
        "SampledCubeArray", /* (45), */
        "SampledBuffer", /* (46), */
        "ImageBuffer", /* (47), */
        "ImageMSArray", /* (48), */
        "StorageImageExtendedFormats", /* (49), */
        "ImageQuery", /* (50), */
        "DerivativeControl", /* (51), */
        "InterpolationFunction", /* (52), */
        "TransformFeedback", /* (53), */
        "GeometryStreams", /* (54), */
        "StorageImageReadWithoutFormat", /* (55), */
        "StorageImageWriteWithoutFormat", /* (56), */
        "MultiViewport", /* (57), */
        "SubgroupDispatch", /* (58), */
        "NamedBarrier", /* (59), */
        "PipeStorage", /* (60), */
        "GroupNonUniform", /* (61), */
        "GroupNonUniformVote", /* (62), */
        "GroupNonUniformArithmetic", /* (63), */
        "GroupNonUniformBallot", /* (64), */
        "GroupNonUniformShuffle", /* (65), */
        "GroupNonUniformShuffleRelative", /* (66), */
        "GroupNonUniformClustered", /* (67), */
        "GroupNonUniformQuad", /* (68), */
        "ShaderLayer", /* (69), */
        "ShaderViewportIndex", /* (70), */
        "UniformDecoration", /* (71), */
	/* HOLE */
        "CoreBuiltinsARM", /* (4165), */
        "TileImageColorReadAccessEXT", /* (4166), */
        "TileImageDepthReadAccessEXT", /* (4167), */
        "TileImageStencilReadAccessEXT", /* (4168), */
	/* HOLE */
        "TensorsARM", /* (4174), */
        "StorageTensorArrayDynamicIndexingARM", /* (4175), */
        "StorageTensorArrayNonUniformIndexingARM", /* (4176), */
	/* HOLE */
        "GraphARM", /* (4191), */
	/* HOLE */
        "CooperativeMatrixLayoutsARM", /* (4201), */
	/* HOLE */
        "Float8EXT", /* (4212), */
        "Float8CooperativeMatrixEXT", /* (4213), */
	/* HOLE */
        "Float6EXT", /* (4228), */
        "Float4EXT", /* (4229), */
        "Float8UnsignedE8M0EXT", /* (4230), */
        "MXInt8EXT", /* (4231), */
        "BitcastExtractEXT", /* (4232), */
	/* HOLE */
        "FragmentShadingRateKHR", /* (4422), */
        "SubgroupBallotKHR", /* (4423), */
	/* HOLE */
        "DrawParameters", /* (4427), */
        "WorkgroupMemoryExplicitLayoutKHR", /* (4428), */
        "WorkgroupMemoryExplicitLayout8BitAccessKHR", /* (4429), */
        "WorkgroupMemoryExplicitLayout16BitAccessKHR", /* (4430), */
        "SubgroupVoteKHR", /* (4431), */
        "<unknown>", /* (4432), */
        "StorageBuffer16BitAccess / StorageUniformBufferBlock16", /* (4433), */
        "StorageUniform16 / UniformAndStorageBuffer16BitAccess", /* (4434), */
        "StoragePushConstant16", /* (4435), */
        "StorageInputOutput16", /* (4436), */
        "DeviceGroup", /* (4437), */
        "<unknown>", /* (4438), */
        "MultiView", /* (4439), */
        "<unknown>", /* (4440), */
        "VariablePointersStorageBuffer", /* (4441), */
        "VariablePointers", /* (4442), */
        "<unknown>", /* (4443), */
        "<unknown>", /* (4444), */
        "AtomicStorageOps", /* (4445), */
        "<unknown>", /* (4446), */
        "SampleMaskPostDepthCoverage", /* (4447), */
        "StorageBuffer8BitAccess", /* (4448), */
        "UniformAndStorageBuffer8BitAccess", /* (4449), */
        "StoragePushConstant8", /* (4450), */
	/* HOLE */
        "DenormPreserve", /* (4464), */
        "DenormFlushToZero", /* (4465), */
        "SignedZeroInfNanPreserve", /* (4466), */
        "RoundingModeRTE", /* (4467), */
        "RoundingModeRTZ", /* (4468), */
        "<unknown>", /* (4469), */
        "<unknown>", /* (4470), */
        "RayQueryProvisionalKHR", /* (4471), */
        "RayQueryKHR", /* (4472), */
        "UntypedPointersKHR", /* (4473), */
	/* HOLE */
        "RayTraversalPrimitiveCullingKHR", /* (4478), */
        "RayTracingKHR", /* (4479), */
	/* HOLE */
        "TextureSampleWeightedQCOM", /* (4484), */
        "TextureBoxFilterQCOM", /* (4485), */
        "TextureBlockMatchQCOM", /* (4486), */
	/* HOLE */
        "TileShadingQCOM", /* (4495), */
        "CooperativeMatrixConversionQCOM", /* (4496), */
        "<unknown>", /* (4497), */
        "TextureBlockMatch2QCOM", /* (4498), */
	/* HOLE */
        "BFloat16MulAddQCOM", /* (4504), */
        "<unknown>", /* (4505), */
        "SubgroupSizeQCOM", /* (4506), */
	/* HOLE */
        "MultipleWaitQueuesQCOM", /* (4539), */
	/* HOLE */
        "ImageGatherLinearQCOM", /* (4543), */
        "ImageGatherExtendedModesQCOM", /* (4544), */
	/* HOLE */
        "Float16ImageAMD", /* (5008), */
        "ImageGatherBiasLodAMD", /* (5009), */
        "FragmentMaskAMD", /* (5010), */
        "<unknown>", /* (5011), */
        "<unknown>", /* (5012), */
        "StencilExportEXT", /* (5013), */
        "<unknown>", /* (5014), */
        "ImageReadWriteLodAMD", /* (5015), */
        "Int64ImageEXT", /* (5016), */
	/* HOLE */
        "ShaderClockKHR", /* (5055), */
	/* HOLE */
        "ShaderEnqueueAMDX", /* (5067), */
	/* HOLE */
        "QuadControlKHR", /* (5087), */
	/* HOLE */
        "Int4TypeINTEL", /* (5112), */
        "<unknown>", /* (5113), */
        "Int4CooperativeMatrixINTEL", /* (5114), */
        "<unknown>", /* (5115), */
        "BFloat16TypeKHR", /* (5116), */
        "BFloat16DotProductKHR", /* (5117), */
        "BFloat16CooperativeMatrixKHR", /* (5118), */
        "<unknown>", /* (5119), */
        "AbortKHR", /* (5120), */
	/* HOLE */
        "DescriptorHeapEXT", /* (5128), */
	/* HOLE */
        "ConstantDataKHR", /* (5146), */
	/* HOLE */
        "PoisonFreezeKHR", /* (5156), */
	/* HOLE */
        "WeakLinkageAMD", /* (5181), */
	/* HOLE */
        "SampleMaskOverrideCoverageNV", /* (5249), */
        "<unknown>", /* (5250), */
        "GeometryShaderPassthroughNV", /* (5251), */
        "<unknown>", /* (5252), */
        "<unknown>", /* (5253), */
        "ShaderViewportIndexLayerEXT / ShaderViewportIndexLayerNV", /* (5254), */
        "ShaderViewportMaskNV", /* (5255), */
	/* HOLE */
        "ShaderStereoViewNV", /* (5259), */
        "PerViewAttributesNV", /* (5260), */
	/* HOLE */
        "FragmentFullyCoveredEXT", /* (5265), */
        "MeshShadingNV", /* (5266), */
	/* HOLE */
        "ImageFootprintNV", /* (5282), */
        "MeshShadingEXT", /* (5283), */
        "FragmentBarycentricKHR / FragmentBarycentricNV ", /* (5284), */
	/* HOLE */
        "ComputeDerivativeGroupQuadsKHR / ComputeDerivativeGroupQuadsNV", /* (5288), */
	/* HOLE */
        "FragmentDensityEXT / ShadingRateNV", /* (5291), */
	/* HOLE */
        "GroupNonUniformPartitionedEXT / GroupNonUniformPartitionedNV", /* (5297), */
	/* HOLE */
        "ShaderNonUniform / ShaderNonUniformEXT", /* (5301), */
        "RuntimeDescriptorArray / RuntimeDescriptorArrayEXT", /* (5302), */
        "InputAttachmentArrayDynamicIndexing / InputAttachmentArrayDynamicIndexingEXT", /* (5303), */
        "UniformTexelBufferArrayDynamicIndexing / UniformTexelBufferArrayDynamicIndexingEXT", /* (5304), */
        "StorageTexelBufferArrayDynamicIndexing / StorageTexelBufferArrayDynamicIndexingEXT", /* (5305), */
        "UniformBufferArrayNonUniformIndexing / UniformBufferArrayNonUniformIndexingEXT", /* (5306), */
        "SampledImageArrayNonUniformIndexing / SampledImageArrayNonUniformIndexingEXT", /* (5307), */
        "StorageBufferArrayNonUniformIndexing / StorageBufferArrayNonUniformIndexingEXT", /* (5308), */
        "StorageImageArrayNonUniformIndexing / StorageImageArrayNonUniformIndexingEXT", /* (5309), */
        "InputAttachmentArrayNonUniformIndexing / InputAttachmentArrayNonUniformIndexingEXT", /* (5310), */
        "UniformTexelBufferArrayNonUniformIndexing / UniformTexelBufferArrayNonUniformIndexingEXT", /* (5311), */
        "StorageTexelBufferArrayNonUniformIndexing / StorageTexelBufferArrayNonUniformIndexingEXT", /* (5312), */
	/* HOLE */
        "RayTracingPositionFetchKHR", /* (5336), */
	/* HOLE */
        "RayTracingNV", /* (5340), */
        "RayTracingMotionBlurNV", /* (5341), */
	/* HOLE */
        "VulkanMemoryModel / VulkanMemoryModelKHR", /* (5345), */
        "VulkanMemoryModelDeviceScope / VulkanMemoryModelDeviceScopeKHR", /* (5346), */
        "PhysicalStorageBufferAddresses / PhysicalStorageBufferAddressesEXT", /* (5347), */
	/* HOLE */
        "ComputeDerivativeGroupLinearKHR / ComputeDerivativeGroupLinearNV", /* (5350), */
	/* HOLE */
        "RayTracingProvisionalKHR", /* (5353), */
	/* HOLE */
        "CooperativeMatrixNV", /* (5357), */
	/* HOLE */
        "FragmentShaderSampleInterlockEXT", /* (5363), */
	/* HOLE */
        "FragmentShaderShadingRateInterlockEXT", /* (5372), */
        "ShaderSMBuiltinsNV", /* (5373), */
	/* HOLE */
        "FragmentShaderPixelInterlockEXT", /* (5378), */
        "DemoteToHelperInvocation / DemoteToHelperInvocationEXT", /* (5379), */
        "DisplacementMicromapNV", /* (5380), */
        "RayTracingOpacityMicromapEXT / RayTracingOpacityMicromapKHR", /* (5381), */
	"<unknown>", /* (5382), */
        "ShaderInvocationReorderNV", /* (5383), */
	/* HOLE */
        "ShaderInvocationReorderEXT", /* (5388), */
	"<unknown>", /* (5389), */
        "BindlessTextureNV", /* (5390), */
        "RayQueryPositionFetchKHR", /* (5391), */
	"<unknown>", /* (5392), */
	"<unknown>", /* (5393), */
        "CooperativeVectorNV", /* (5394), */
	/* HOLE */
        "AtomicFloat16VectorNV", /* (5404), */
	/* HOLE */
        "RayTracingDisplacementMicromapNV", /* (5409), */
	/* HOLE */
        "RawAccessChainsNV", /* (5414), */
	/* HOLE */
        "RayTracingSpheresGeometryNV", /* (5418), */
        "RayTracingLinearSweptSpheresGeometryNV", /* (5419), */
	/* HOLE */
        "PushConstantBanksNV", /* (5423), */
	"<unknown>", /* (5424), */
        "LongVectorEXT", /* (5425), */
        "Shader64BitIndexingEXT", /* (5426), */
	"<unknown>", /* (5427), */
	"<unknown>", /* (5428), */
        "CooperativeMatrixConversionsEXT", /* (5429), */
        "CooperativeMatrixReductionsEXT / CooperativeMatrixReductionsNV", /* (5430), */
        "CooperativeMatrixConversionsNV", /* (5431), */
        "CooperativeMatrixPerElementOperationsEXT / CooperativeMatrixPerElementOperationsNV", /* (5432), */
        "CooperativeMatrixTensorAddressingNV", /* (5433), */
        "CooperativeMatrixBlockLoadsNV", /* (5434), */
        "CooperativeVectorTrainingNV", /* (5435), */
	"<unknown>", /* (5436), */
        "RayTracingClusterAccelerationStructureNV", /* (5437), */
        "CooperativeMatrixGetCoordinateEXT", /* (5438), */
        "TensorAddressingNV", /* (5439), */
	/* HOLE */
        "CooperativeMatrixDecodeVectorNV", /* (5447), */
	/* HOLE */
        "SubgroupShuffleINTEL", /* (5568), */
        "SubgroupBufferBlockIOINTEL", /* (5569), */
        "SubgroupImageBlockIOINTEL", /* (5570), */
	/* HOLE */
        "SubgroupImageMediaBlockIOINTEL", /* (5579), */
	"<unknown>", /* (5580), */
	"<unknown>", /* (5581), */
        "RoundToInfinityINTEL", /* (5582), */
        "FloatingPointModeINTEL", /* (5583), */
        "IntegerFunctions2INTEL", /* (5584), */
	/* HOLE */
        "FunctionPointersINTEL", /* (5603), */
        "IndirectReferencesINTEL", /* (5604), */
	"<unknown>", /* (5605), */
        "AsmINTEL", /* (5606), */
	/* HOLE */
        "AtomicFloat32MinMaxEXT", /* (5612), */
        "AtomicFloat64MinMaxEXT", /* (5613), */
	"<unknown>", /* (5614), */
	"<unknown>", /* (5615), */
        "AtomicFloat16MinMaxEXT", /* (5616), */
        "VectorComputeINTEL", /* (5617), */
	"<unknown>", /* (5618), */
        "VectorAnyINTEL", /* (5619), */
	/* HOLE */
        "ExpectAssumeKHR", /* (5629), */
	/* HOLE */
        "SubgroupAvcMotionEstimationINTEL", /* (5696), */
        "SubgroupAvcMotionEstimationIntraINTEL", /* (5697), */
        "SubgroupAvcMotionEstimationChromaINTEL", /* (5698), */
	/* HOLE */
        "VariableLengthArrayINTEL", /* (5817), */
	/* HOLE */
        "FunctionFloatControlINTEL", /* (5821), */
	"<unknown>", /* (5822), */
	"<unknown>", /* (5823), */
        "FPGAMemoryAttributesALTERA / FPGAMemoryAttributesINTEL", /* (5824), */
	/* HOLE */
        "FPFastMathModeINTEL", /* (5837), */
	/* HOLE */
        "ArbitraryPrecisionIntegersALTERA / ArbitraryPrecisionIntegersINTEL", /* (5844), */
        "ArbitraryPrecisionFloatingPointALTERA / ArbitraryPrecisionFloatingPointINTEL", /* (5845), */
	/* HOLE */
        "UnstructuredLoopControlsINTEL", /* (5886), */
	"<unknown>", /* (5887), */
        "FPGALoopControlsALTERA / FPGALoopControlsINTEL", /* (5888), */
	/* HOLE */
        "KernelAttributesINTEL", /* (5892), */
	/* HOLE */
        "FPGAKernelAttributesINTEL", /* (5897), */
        "FPGAMemoryAccessesALTERA / FPGAMemoryAccessesINTEL", /* (5898), */
	/* HOLE */
        "FPGAClusterAttributesALTERA / FPGAClusterAttributesINTEL", /* (5904), */
        "<unknown>", /* (5905), */
        "LoopFuseALTERA / LoopFuseINTEL", /* (5906), */
        "<unknown>", /* (5907), */
        "FPGADSPControlALTERA / FPGADSPControlINTEL", /* (5908), */
        "<unknown>", /* (5909), */
        "MemoryAccessAliasingINTEL", /* (5910), */
	/* HOLE */
        "FPGAInvocationPipeliningAttributesALTERA / FPGAInvocationPipeliningAttributesINTEL", /* (5916), */
        "FPGABufferLocationALTERA / FPGABufferLocationINTEL", /* (5920), */
        "<unknown>", /* (5921), */
        "ArbitraryPrecisionFixedPointALTERA / ArbitraryPrecisionFixedPointINTEL", /* (5922), */
	/* HOLE */
        "USMStorageClassesALTERA / USMStorageClassesINTEL", /* (5935), */
	/* HOLE */
        "RuntimeAlignedAttributeALTERA / RuntimeAlignedAttributeINTEL", /* (5939), */
	/* HOLE */
        "IOPipesALTERA / IOPipesINTEL", /* (5943), */
        "<unknown>", /* (5944), */
        "BlockingPipesALTERA / BlockingPipesINTEL", /* (5945), */
        "<unknown>", /* (5946), */
        "<unknown>", /* (5947), */
        "FPGARegALTERA / FPGARegINTEL", /* (5948), */
	/* HOLE */
        "DotProductInputAll / DotProductInputAllKHR", /* (6016), */
        "DotProductInput4x8Bit / DotProductInput4x8BitKHR", /* (6017), */
        "DotProductInput4x8BitPacked / DotProductInput4x8BitPackedKHR", /* (6018), */
        "DotProduct / DotProductKHR", /* (6019), */
        "RayCullMaskKHR", /* (6020), */
        "<unknown>", /* (6021), */
        "CooperativeMatrixKHR", /* (6022), */
        "<unknown>", /* (6023), */
        "ReplicatedCompositesEXT", /* (6024), */
        "BitInstructions", /* (6025), */
        "GroupNonUniformRotateKHR", /* (6026), */
        "<unknown>", /* (6027), */
        "<unknown>", /* (6028), */
        "FloatControls2", /* (6029), */
        "FMAKHR", /* (6030), */
        "<unknown>", /* (6031), */
        "RayTracingOpacityMicromapExecutionModeKHR", /* (6032), */
        "AtomicFloat32AddEXT", /* (6033), */
        "AtomicFloat64AddEXT", /* (6034), */
	/* HOLE */
        "LongCompositesINTEL", /* (6089), */
	/* HOLE */
        "OptNoneEXT / OptNoneINTEL", /* (6094), */
        "AtomicFloat16AddEXT", /* (6095), */
	/* HOLE */
        "DebugInfoModuleINTEL", /* (6114), */
        "BFloat16ConversionINTEL", /* (6115), */
	/* HOLE */
        "SplitBarrierEXT / SplitBarrierINTEL", /* (6141), */
	/* HOLE */
        "ArithmeticFenceEXT", /* (6144), */
	/* HOLE */
        "FPGAClusterAttributesV2ALTERA / FPGAClusterAttributesV2INTEL", /* (6150), */
	/* HOLE */
        "FPGAKernelAttributesv2INTEL", /* (6161), */
        "TaskSequenceALTERA / TaskSequenceINTEL", /* (6162), */
	/* HOLE */
        "FPMaxErrorINTEL", /* (6169), */
        "<unknown>", /* (6170), */
        "FPGALatencyControlALTERA / FPGALatencyControlINTEL", /* (6171), */
        "<unknown>", /* (6172), */
        "<unknown>", /* (6173), */
        "FPGAArgumentInterfacesALTERA / FPGAArgumentInterfacesINTEL", /* (6174), */
	/* HOLE */
        "DeviceBarrierINTEL", /* (6185), */
        "<unknown>", /* (6186), */
        "GlobalVariableHostAccessINTEL", /* (6187), */
        "<unknown>", /* (6188), */
        "GlobalVariableFPGADecorationsALTERA / GlobalVariableFPGADecorationsINTEL", /* (6189), */
	/* HOLE */
        "SubgroupBitcastShuffleINTEL", /* (6207), */
	/* HOLE */
        "SubgroupBufferPrefetchINTEL", /* (6220), */
	/* HOLE */
        "Subgroup2DBlockIOINTEL", /* (6228), */
        "Subgroup2DBlockTransformINTEL", /* (6229), */
        "Subgroup2DBlockTransposeINTEL", /* (6230), */
	/* HOLE */
        "SubgroupMatrixMultiplyAccumulateINTEL", /* (6236), */
	/* HOLE */
        "TernaryBitwiseFunctionINTEL", /* (6241), */
	"<unknown>", /* (6242), */
        "UntypedVariableLengthArrayINTEL", /* (6243), */
	"<unknown>", /* (6244), */
        "SpecConditionalINTEL", /* (6245), */
        "FunctionVariantsINTEL", /* (6246), */
	/* HOLE */
        "PredicatedIOINTEL", /* (6257), */
	/* HOLE */
        "RoundedDivideSqrtINTEL", /* (6265), */
	/* HOLE */
        "GroupUniformArithmeticKHR", /* (6400), */
	/* HOLE */
        "TensorFloat32RoundingINTEL", /* (6425), */
	"<unknown>", /* (6426), */
        "MaskedGatherScatterINTEL", /* (6427), */
	/* HOLE */
        "CacheControlsINTEL", /* (6441), */
	/* HOLE */
        "RegisterLimitsINTEL", /* (6460), */
	/* HOLE */
        "BindlessImagesINTEL", /* (6528), */
	/* HOLE */
        "DotProductFloat16AccFloat32VALVE", /* (6912), */
        "DotProductFloat16AccFloat16VALVE", /* (6913), */
        "DotProductBFloat16AccVALVE", /* (6914), */
        "DotProductFloat8AccFloat32VALVE", /* (6915), */
	/* HOLE */
        "IntrinsicSAMSUNG", /* (7041), */
};
