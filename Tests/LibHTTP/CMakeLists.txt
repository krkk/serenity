set(TEST_SOURCES
    TestHPack.cpp
)

foreach(source IN LISTS TEST_SOURCES)
    serenity_test("${source}" LibHTTP LIBS LibHTTP)
endforeach()
