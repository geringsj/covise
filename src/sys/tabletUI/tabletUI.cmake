USING(Virvo:transfunc optional) # for transfer function editor
USING(WiringPi optional)


# set TUI_SOURCES, TUI_HEADERS and TUI_MOC_HEADERS

set(CMAKE_AUTORCC ON)

SET(TUI_SOURCES
  mapviewer.qrc
  qtcolortriangle.cpp
  qtpropertyDialog.cpp
  TUIColorTriangle.cpp
  TUIColorButton.cpp
  TUIContainer.cpp
  TUITab.cpp
  TUITabFolder.cpp
  TUIProgressBar.cpp
  TUIButton.cpp
  TUIToggleButton.cpp
  TUIToggleBitmapButton.cpp
  TUIFrame.cpp
  TUIGroupBox.cpp
  TUIScrollArea.cpp
  TUISplitter.cpp
  TUIComboBox.cpp
  TUIListBox.cpp
  TUIFloatSlider.cpp
  TUISlider.cpp
  TUIFloatEdit.cpp
  TUIIntEdit.cpp
  TUILabel.cpp
  TUIElement.cpp
  TUILineEdit.cpp
  TUILineCheck.cpp
  # TUITextSpinEdit.cpp
  TUIApplication.cpp
  TUINavElement.cpp
  TUISGBrowserTab.cpp
  TUIColorTab.cpp
  TUIMap.cpp
  TUIEarthMap.cpp
  TUIFileBrowserButton.cpp
  FileBrowser/FileBrowser.cpp
  TUITextCheck.cpp
  TUITextEdit.cpp
  TUIAnnotationTab.cpp
  TUIColorWidget.cpp
  TUIPopUp.cpp
  TUIUITab.cpp
  TUIUI/TUIUIWidget.cpp
  TUIUI/TUIUIWidgetSet.cpp
  TUIUI/TUIUIScriptWidget.cpp
)

SET(TUI_HEADERS
  TUIContainer.h
  TUIElement.h
  TUIFrame.h
  TUIGroupBox.h
  TUILabel.h
  TUINavElement.h
  TUISplitter.h
  TUITab.h
  FileBrowser/RemoteClientDialog.h
)

SET(TUI_MOC_HEADERS
  qtcolortriangle.h
  qtpropertyDialog.h
  TUIColorTriangle.h
  TUIColorButton.h
  TUISGBrowserTab.h
  TUIColorTab.h
  TUIButton.h
  TUIComboBox.h
  TUIIntEdit.h
  TUIProgressBar.h
  TUIFloatEdit.h
  TUIFloatSlider.h
  TUISlider.h
  TUILineEdit.h
  TUILineCheck.h
  TUIListBox.h
  TUIEarthMap.h
  TUIMap.h
  TUITabFolder.h
  # TUITextSpinEdit.h
  TUIApplication.h
  TUIToggleButton.h
  TUIToggleBitmapButton.h
  TUIFileBrowserButton.h
  FileBrowser/FileBrowser.h
  TUITextCheck.h
  TUITextEdit.h
  TUIAnnotationTab.h
  TUIColorWidget.h
  TUIPopUp.h
  TUIUITab.h
  TUIUI/TUIUIWidget.h
  TUIUI/TUIUIWidgetSet.h
  TUIUI/TUIUIScriptWidget.h
)
if (WIRINGPI_FOUND)
   SET(TUI_SOURCES ${TUI_SOURCES}
      Thyssen.cpp
      ThyssenButton.cpp
      )
   SET(TUI_HEADERS ${TUI_HEADERS}
      Thyssen.h
      ThyssenButton.h
      )
endif (WIRINGPI_FOUND)


if (COVISE_USE_VIRVO)
   SET(TUI_SOURCES ${TUI_SOURCES}
      TUIFunctionEditorTab.cpp
      TUITFEWidgets.cpp
      TUITFEditor.cpp
      TUITF2DEditor.cpp
      )
   SET(TUI_HEADERS ${TUI_HEADERS}
      TUITFEWidgets.h
      )
   SET(TUI_MOC_HEADERS ${TUI_MOC_HEADERS}
      TUIFunctionEditorTab.h
      TUITFEditor.h
      TUITF2DEditor.h
      )
endif()


if (COVISE_USE_WEBENGINE)
   SET(TUI_MOC_HEADERS ${TUI_MOC_HEADERS}
     TUIWebview.h
   )
   SET(TUI_SOURCES ${TUI_SOURCES}
     TUIWebview.cpp
   )
endif()
