add_library(mlog-obj OBJECT)
target_include_directories(mlog-obj PUBLIC .)
target_sources(mlog-obj PRIVATE mlog.c)
