serenity_component(
    FileManager
    REQUIRED
    TARGETS FileManager
    DEPENDS FileOperation
)

stringify_gml(FileManagerWindow.gml FileManagerWindowGML.h file_manager_window_gml)
stringify_gml(FileOperationProgress.gml FileOperationProgressGML.h file_operation_progress_gml)
compile_gml(Properties/ArchiveTab.gml Properties/ArchiveTabGML.cpp)
compile_gml(Properties/AudioTab.gml Properties/AudioTabGML.cpp)
compile_gml(Properties/FontTab.gml Properties/FontTabGML.cpp)
compile_gml(Properties/GeneralTab.gml Properties/GeneralTabGML.cpp)
compile_gml(Properties/ImageTab.gml Properties/ImageTabGML.cpp)
compile_gml(Properties/PDFTab.gml Properties/PDFTabGML.cpp)

set(SOURCES
    DesktopWidget.cpp
    DirectoryView.cpp
    FileOperationProgressWidget.cpp
    FileUtils.cpp
    main.cpp
    Properties/PropertiesWindow.cpp
    Properties/ArchiveTabGML.cpp
    Properties/AudioTabGML.cpp
    Properties/FontTabGML.cpp
    Properties/GeneralTabGML.cpp
    Properties/ImageTabGML.cpp
    Properties/PDFTabGML.cpp
)

set(GENERATED_SOURCES
    FileManagerWindowGML.h
    FileOperationProgressGML.h
)

serenity_app(FileManager ICON app-file-manager)
target_link_libraries(FileManager PRIVATE LibArchive LibAudio LibConfig LibCore LibDesktop LibFileSystem LibGfx LibGUI LibMain LibMaps LibPDF LibThreading LibURL)
