qt_details_version := 6.8.1
qt_details_download_path := https://download.qt.io/archive/qt/6.8/$(qt_details_version)/submodules
qt_details_suffix := everywhere-src-$(qt_details_version).tar.xz

qt_details_qtbase_file_name := qtbase-$(qt_details_suffix)
qt_details_qtbase_sha256_hash := 40b14562ef3bd779bc0e0418ea2ae08fa28235f8ea6e8c0cb3bce1d6ad58dcaf

qt_details_qttranslations_file_name := qttranslations-$(qt_details_suffix)
qt_details_qttranslations_sha256_hash := 635a6093e99152243b807de51077485ceadd4786d4acb135b9340b2303035a4a

qt_details_qttools_file_name := qttools-$(qt_details_suffix)
qt_details_qttools_sha256_hash := 9d43d409be08b8681a0155a9c65114b69c9a3fc11aef6487bb7fdc5b283c432d

qt_details_patches_path := $(PATCHES_PATH)/qt

qt_details_top_download_path := https://code.qt.io/cgit/qt/qt5.git/plain
qt_details_top_cmakelists_file_name := CMakeLists.txt
qt_details_top_cmakelists_download_file := $(qt_details_top_cmakelists_file_name)?h=$(qt_details_version)
qt_details_top_cmakelists_sha256_hash := 54e9a4e554da37792446dda4f52bc308407b01a34bcc3afbad58e4e0f71fac9b
qt_details_top_cmake_download_path := $(qt_details_top_download_path)/cmake
qt_details_top_cmake_ecmoptionaladdsubdirectory_file_name := ECMOptionalAddSubdirectory.cmake
qt_details_top_cmake_ecmoptionaladdsubdirectory_download_file := $(qt_details_top_cmake_ecmoptionaladdsubdirectory_file_name)?h=$(qt_details_version)
qt_details_top_cmake_ecmoptionaladdsubdirectory_sha256_hash := 97ee8bbfcb0a4bdcc6c1af77e467a1da0c5b386c42be2aa97d840247af5f6f70
qt_details_top_cmake_qttoplevelhelpers_file_name := QtTopLevelHelpers.cmake
qt_details_top_cmake_qttoplevelhelpers_download_file := $(qt_details_top_cmake_qttoplevelhelpers_file_name)?h=$(qt_details_version)
qt_details_top_cmake_qttoplevelhelpers_sha256_hash := e11581b2101a6836ca991817d43d49e1f6016e4e672bbc3523eaa8b3eb3b64c2
