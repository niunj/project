MAKE SURE THE ENVIRONMENT VARIABLE SILVERLINING_PATH IS DEFINED to point 
to the directory containing the SilverLining SDK. Under Windows, this is 
set automatically by the SDK installer, but Linux and Mac users will need 
to set this by hand.

This sample is based on the osgViewer sample application, and has been 
tested with OSG 2.4 through 3.4.0

To build it, you must first set the OSGDIR environment variable to the 
root of your OpenSceneGraph installation. Run cmake 2.6 or newer on 
CMakeLists.txt, and the makefiles for your compiler will be generated.

To run the sample under Windows, you must set its working directory to the 
bin directory of your OpenSceneGraph installation so the OSG DLL's will be 
found.

Pass it a single parameter to the path of an .osg file you wish 
to visualize (with SilverLining's sky and clouds behind it.)

It does not handle the case of multiple contexts, such as full-screen windows 
spanning multiple monitors. If you need to handle that situation, instantiate 
a seperate Atmosphere class for each context, and use the appropriate one at 
draw time.

For more information, see the SilverLining documentation for OpenSceneGraph 
integration.

As always, if you run into any trouble, don't hesitate to contact 
support@sundog-soft.com for assistance.
