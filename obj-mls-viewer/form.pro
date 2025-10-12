QT       += core gui widgets




win32 {
        #3rdparty
        MTE_3RDPARTY_PATH = $$PWD/3rdparty_windows

        #osg
        INCLUDEPATH += $$MTE_3RDPARTY_PATH/osg365/include
        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losg

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgDBd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgDB

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgFXd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgFX

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgGAd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgGA

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgManipulatord
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgManipulator

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgParticled
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgParticle

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgTextd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgText

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgTerraind
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgTerrain

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgUtild
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgUtil

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgViewerd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgViewer

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgVolumed
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgVolume

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgWidgetd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgWidget

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -lOpenThreadsd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -lOpenThreads

        #vtk
        INCLUDEPATH += $$MTE_3RDPARTY_PATH/vtk/include

        #CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lQVTKWidgetPlugind
        #CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lQVTKWidgetPlugin

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkChartsCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkChartsCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonColor-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonColor-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonComputationalGeometry-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonComputationalGeometry-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonDataModel-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonDataModel-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonExecutionModel-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonExecutionModel-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonMath-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonMath-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonMisc-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonMisc-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonSystem-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonSystem-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonTransforms-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkCommonTransforms-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkDICOMParser-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkDICOMParser-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkDomainsChemistry-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkDomainsChemistry-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkDomainsChemistryOpenGL2-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkDomainsChemistryOpenGL2-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkdoubleconversion-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkdoubleconversion-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkexodusII-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkexodusII-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkexpat-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkexpat-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersAMR-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersAMR-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersExtraction-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersExtraction-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersFlowPaths-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersFlowPaths-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersGeneral-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersGeneral-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersGeneric-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersGeneric-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersGeometry-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersGeometry-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersHybrid-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersHybrid-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersHyperTree-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersHyperTree-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersImaging-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersImaging-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersModeling-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersModeling-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersParallel-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersParallel-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersParallelImaging-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersParallelImaging-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersPoints-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersPoints-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersProgrammable-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersProgrammable-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersSelection-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersSelection-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersSMP-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersSMP-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersSources-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersSources-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersStatistics-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersStatistics-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersTexture-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersTexture-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersTopology-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersTopology-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersVerdict-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkFiltersVerdict-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkfreetype-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkfreetype-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkGeovisCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkGeovisCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkgl2ps-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkgl2ps-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkglew-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkglew-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkGUISupportQt-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkGUISupportQt-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkGUISupportQtSQL-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkGUISupportQtSQL-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkhdf5-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkhdf5-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkhdf5_hl-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkhdf5_hl-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingColor-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingColor-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingFourier-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingFourier-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingGeneral-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingGeneral-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingHybrid-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingHybrid-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingMath-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingMath-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingMorphological-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingMorphological-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingSources-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingSources-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingStatistics-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingStatistics-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingStencil-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkImagingStencil-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInfovisCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInfovisCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInfovisLayout-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInfovisLayout-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInteractionImage-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInteractionImage-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInteractionStyle-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInteractionStyle-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInteractionStyle-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInteractionStyle-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOAMR-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOAMR-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOAsynchronous-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOAsynchronous-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOCityGML-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOCityGML-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOEnSight-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOEnSight-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOExodus-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOExodus-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOExport-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOExport-9.3

        #CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOExportOpenGL2-9.3d
        #CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOExportOpenGL2-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOExportPDF-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOExportPDF-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOGeometry-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOGeometry-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOImage-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOImage-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOImport-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOImport-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOInfovis-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOInfovis-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOLegacy-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOLegacy-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOLSDyna-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOLSDyna-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOMINC-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOMINC-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOMovie-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOMovie-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIONetCDF-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIONetCDF-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOParallel-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOParallel-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOParallelXML-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOParallelXML-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOPLY-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOPLY-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOSegY-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOSegY-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOSegY-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOSegY-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOSQL-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOSQL-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOTecplotTable-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOTecplotTable-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOVeraOut-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOVeraOut-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOVideo-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOVideo-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOXML-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOXML-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOXMLParser-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkIOXMLParser-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkjpeg-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkjpeg-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkjsoncpp-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkjsoncpp-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtklibharu-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtklibharu-9.3

        #CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtklibxml2-8.2d
        #CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtklibxml2-8.2

        #CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkLocalExample-8.2d
        #CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkLocalExample-8.2

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtklz4-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtklz4-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtklzma-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtklzma-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkmetaio-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkmetaio-9.3

        #CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkmyCommond
        #CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkmyCommon

        #CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkmyImagingd
        #CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkmyImaging

        #CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkmyUnsortedd
        #CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkmyUnsorted

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkNetCDF-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkNetCDF-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkogg-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkogg-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkParallelCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkParallelCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkpng-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkpng-9.3

        #CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkproj-9.3d
        #CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkproj-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkpugixml-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkpugixml-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingAnnotation-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingAnnotation-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingContext2D-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingContext2D-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingContextOpenGL2-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingContextOpenGL2-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingFreeType-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingFreeType-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingGL2PSOpenGL2-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingGL2PSOpenGL2-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingImage-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingImage-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingLabel-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingLabel-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingLOD-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingLOD-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingOpenGL2-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingOpenGL2-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingQt-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingQt-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingVolume-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingVolume-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingVolumeOpenGL2-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkRenderingVolumeOpenGL2-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtksqlite-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtksqlite-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtksys-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtksys-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtktheora-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtktheora-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtktiff-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtktiff-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkverdict-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkverdict-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkViewsContext2D-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkViewsContext2D-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkViewsCore-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkViewsCore-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkViewsInfovis-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkViewsInfovis-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkViewsQt-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkViewsQt-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkzlib-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkzlib-9.3

        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInteractionWidgets-9.3d
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/vtk/lib -lvtkInteractionWidgets-9.3

        #osgEarth
        INCLUDEPATH += $$MTE_3RDPARTY_PATH/oe32/include
        CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/oe32/lib -losgEarthd
        CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/oe32/lib -losgEarth



}

unix {
    # 第三方根路径
    MTE_3RDPARTY_PATH = $$PWD/3rdparty_linux

    # ================================
    # Include Path (头文件)
    # ================================
    INCLUDEPATH += \
        $$MTE_3RDPARTY_PATH/osg365/include \
        $$MTE_3RDPARTY_PATH/vtk930/include/vtk-9.3 \


    # ================================
    # OpenSceneGraph 库
    # ================================
    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losg

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgDBd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgDB

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgFXd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgFX

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgGAd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgGA

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgManipulatord
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgManipulator

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgParticled
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgParticle

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgTextd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgText

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgTerraind
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgTerrain

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgUtild
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgUtil

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgViewerd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgViewer

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgVolumed
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgVolume

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgWidgetd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgWidget

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -lOpenThreadsd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -lOpenThreads

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgSimd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgSim

    CONFIG(debug, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgShadowd
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/osg365/lib -losgShadow

    # ================================
    # osgEarth
    # ================================
    CONFIG(debug, debug|release):   LIBS += -L$$MTE_3RDPARTY_PATH/oe32/lib64 -losgEarth
    CONFIG(release, debug|release): LIBS += -L$$MTE_3RDPARTY_PATH/oe32/lib64 -losgEarth


    # ================================
    # VTK 9.3.0
    # ================================
    CONFIG(release, debug|release): {
        LIBS += -L$$MTE_3RDPARTY_PATH/vtk930/lib \
            -lvtkCommonCore-9.3 \
            -lvtkCommonColor-9.3 \
            -lvtkCommonDataModel-9.3 \
            -lvtkCommonExecutionModel-9.3 \
            -lvtkFiltersCore-9.3 \
            -lvtkFiltersGeneral-9.3 \
            -lvtkFiltersFlowPaths-9.3 \
            -lvtkFiltersSources-9.3 \
            -lvtkIOImage-9.3 \
            -lvtkIOXML-9.3 \
            -lvtkImagingStatistics-9.3 \
            -lvtkInteractionStyle-9.3 \
            -lvtkInteractionWidgets-9.3 \
            -lvtkRenderingCore-9.3 \
            -lvtkRenderingOpenGL2-9.3 \
            -lvtkRenderingAnnotation-9.3 \
            -lvtkRenderingQt-9.3 \
            -lvtkGeovisCore-9.3 \
            -lvtkGUISupportQt-9.3 \
            -lvtkzlib-9.3 \
            -lvtksys-9.3
    }


    # ================================
    # 系统库
    # ================================
    LIBS += -lGL -lGLU -lglut -lpthread -ldl
}


greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ObjMlsViewer
TEMPLATE = app

# 源文件
SOURCES += \
    main.cpp \
    mainwindow.cpp \
    objviewerwidget.cpp \
    mlsreader.cpp

# 头文件
HEADERS += \
    mainwindow.h \
    objviewerwidget.h \
    mlsreader.h

# UI文件
FORMS +=

# 资源文件（如果有）
# RESOURCES += \
#     resources.qrc

# 编译选项
QMAKE_CXXFLAGS += -std=c++11

# 平台特定配置
unix {
    target.path = /usr/local/bin
    INSTALLS += target
}

win32 {
    # Windows特定配置
    DEFINES += _CRT_SECURE_NO_WARNINGS
}
