Build instructions:

-On Linux, make sure SilverLining has already been built with Vulkan support (via "make vulkan" at the top-level directory of the SDK)
-set the source in CMake to the full path of this directory: \SampleCode\VulkanSceneGraphExample
-set where to build to the full path of this directory: \SampleCode\VulkanSceneGraphExample
-set CMAKE_INSTALLATION_PREFIX to the full path of this directory: \SampleCode\VulkanSceneGraphExample
-set the VSG_DIR to the full path of lib/cmake/vsg under your VulkanSceneGraph install directory

The resulting application should show a teapot rendered by VSG, with our sky and clouds behind it.

Some key bits of code: the SilverLiningGroup constructor illustrates how to initialize SilverLining with integration into VulkanSceneGraph. The SilverLiningCommand class illustrates integration of the sky and clouds into each frame under VSG.