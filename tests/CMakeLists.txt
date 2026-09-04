add_executable(wf_smoke smoke.cpp)
target_link_libraries(wf_smoke PRIVATE WorkloadFabric::WorkloadFabric)
add_test(NAME wf_smoke COMMAND wf_smoke)

add_executable(wf_core test_core.cpp)
target_link_libraries(wf_core PRIVATE WorkloadFabric::WorkloadFabric)
add_test(NAME wf_core COMMAND wf_core)

add_executable(wf_scenarios test_scenarios.cpp)
target_link_libraries(wf_scenarios PRIVATE WorkloadFabric::WorkloadFabric)
add_test(NAME wf_scenarios COMMAND wf_scenarios)

add_executable(wf_adversarial test_adversarial.cpp)
target_link_libraries(wf_adversarial PRIVATE WorkloadFabric::WorkloadFabric)
add_test(NAME wf_adversarial COMMAND wf_adversarial)

add_executable(wf_property property.cpp)
target_link_libraries(wf_property PRIVATE WorkloadFabric::WorkloadFabric)
add_test(NAME wf_property COMMAND wf_property)

add_executable(wf_concurrency concurrency.cpp)
target_link_libraries(wf_concurrency PRIVATE WorkloadFabric::WorkloadFabric)
add_test(NAME wf_concurrency COMMAND wf_concurrency)

add_executable(wf_mp_proof mp_proof.cpp)
target_link_libraries(wf_mp_proof PRIVATE WorkloadFabric::WorkloadFabric)
add_test(NAME wf_mp_proof COMMAND wf_mp_proof $<TARGET_FILE:wf_worker>)
