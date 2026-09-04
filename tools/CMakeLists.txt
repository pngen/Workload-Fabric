add_executable(wf_worker worker_main.cpp)
target_link_libraries(wf_worker PRIVATE WorkloadFabric::WorkloadFabric)

add_executable(wf_inspect wf_inspect.cpp)
target_link_libraries(wf_inspect PRIVATE WorkloadFabric::WorkloadFabric)
