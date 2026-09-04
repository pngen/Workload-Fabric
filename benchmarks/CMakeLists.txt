add_executable(wf_bench bench.cpp)
target_link_libraries(wf_bench PRIVATE WorkloadFabric::WorkloadFabric)
