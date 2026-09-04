# CUDA-backed proof on the local RTX 5090 (sm_120). This is a single-device proof
# of workload lifecycle semantics over real GPU work; it is explicitly NOT
# multi-GPU / multi-node / MIG / RDMA / NVLink evidence.
add_executable(wf_cuda_proof workload_cuda_proof.cu)
target_link_libraries(wf_cuda_proof PRIVATE WorkloadFabric::WorkloadFabric)

set_target_properties(wf_cuda_proof PROPERTIES
    CUDA_STANDARD 20
    CUDA_STANDARD_REQUIRED ON)

# The library's strict /W4 /WX flags are not appropriate for nvcc host compilation
# of CUDA headers; use a reasonable warning level and do not treat warnings as errors.
if(MSVC)
  # nvcc must receive host flags via -Xcompiler so they are not parsed as inputs.
  target_compile_options(wf_cuda_proof PRIVATE -Xcompiler=/W3)
else()
  target_compile_options(wf_cuda_proof PRIVATE -w)
endif()
