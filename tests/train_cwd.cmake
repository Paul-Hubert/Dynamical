# T1.7: started from another directory, dynamical_train writes a relative -envParamsOut under that directory,
# finds its resources next to the executable and creates no config.6.json.
# cmake -DEXE=<TrainingEnv.exe> -DWORK=<empty directory> -P train_cwd.cmake
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
get_filename_component(EXE_DIR "${EXE}" DIRECTORY)
file(REMOVE "${EXE_DIR}/config.6.json")

execute_process(COMMAND "${EXE}" scene=null mode=bench ticks=10 -envParamsOut=x/y.txt
    WORKING_DIRECTORY "${WORK}" RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("${out}${err}")
if (NOT result EQUAL 0)
    message(FATAL_ERROR "exit code ${result}")
endif()
if (NOT EXISTS "${WORK}/x/y.txt")
    message(FATAL_ERROR "${WORK}/x/y.txt was not written")
endif()
file(READ "${WORK}/x/y.txt" content)
message("x/y.txt: ${content}")
if (NOT content MATCHES "^# run=x behavior=")
    message(FATAL_ERROR "unexpected envparams header")
endif()
if (EXISTS "${WORK}/config.6.json" OR EXISTS "${EXE_DIR}/config.6.json")
    message(FATAL_ERROR "config.6.json was created")
endif()
