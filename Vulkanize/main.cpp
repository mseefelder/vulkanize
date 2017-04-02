// Following a tutorial. Currently on this part:
// https://vulkan-tutorial.com/Drawing_a_triangle/Setup/Physical_devices_and_queue_families

//Vulkan functions, structures and enumerations
//Same as "#include <vulkan/vulkan.h>" but with GLFW
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
//Reporting and error propagation
#include <iostream>
#include <stdexcept>
//Lambda functions
#include <functional>
//STD Vector
#include <vector>
// For strcmp
#include <cstring>

const int WIDTH = 800;
const int HEIGHT = 600;

const std::vector<const char*> validationLayers = {
	"VK_LAYER_LUNARG_standard_validation"
};

#ifdef NDEBUG
	const bool enableValidationLayers = false;
#else
	// To configure validation layers better, see
	//the Config folder on the VulkanSDK Directory
	//and read vk_layer_settings
	const bool enableValidationLayers = true;
#endif

 //  Proxy function to find the extension method and create the VkDebugReportCallbackEXT object
 VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) {
     // look up the vkCreateDebugReportCallbackEXT function's address (beacause it's an extension)
     auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
     if (func != nullptr) {
         return func(instance, pCreateInfo, pAllocator, pCallback);
     }
     else {
         return VK_ERROR_EXTENSION_NOT_PRESENT;
     }
 }

 //  Proxy function to find the extension method and destroy the VkDebugReportCallbackEXT object
 void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
	 auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
	 if (func != nullptr) {
		 func(instance, callback, pAllocator);
	 }
 }

//Wrapper to allow automatic Vulkan Object cleanup
template <typename T>
class VDeleter {
public:
	// Default constructor with a dummy deleter function that can be used to initialize 
	//it later, which will be useful for lists of deleters.
	VDeleter() : VDeleter([](T, VkAllocationCallbacks*) {}) {}

	//The three non-default constructors allow you to specify all three types of 
	//deletion functions used in Vulkan:

	//vkDestroyXXX(object, callbacks)
	// Only the object itself needs to be passed to the cleanup function, so we can 
	//simply construct a VDeleter with just the function as argument.
	VDeleter(
		std::function<void(T, VkAllocationCallbacks*)> deletef
	) {
		this->deleter = [=](T obj) { deletef(obj, nullptr); };
	}

	//vkDestroyXXX(instance, object, callbacks)
	// A VkInstance also needs to be passed to the cleanup function, so we use the 
	//VDeleter constructor that takes the VkInstance reference and cleanup function as 
	//parameters.
	VDeleter(
		const VDeleter<VkInstance>& instance, 
		std::function<void(VkInstance, T, VkAllocationCallbacks*)> deletef
	) {
		this->deleter = [&instance, deletef](T obj) { deletef(instance, obj, nullptr); };
	}

	//vkDestroyXXX(device, object, callbacks)
	// Similar to the previous case, but a VkDevice must be passed instead of a 
	//VkInstance.
	VDeleter(
		const VDeleter<VkDevice>& device, 
		std::function<void(VkDevice, T, VkAllocationCallbacks*)> deletef
	) {
		this->deleter = [&device, deletef](T obj) { deletef(device, obj, nullptr); };
	}

	//Ps.: The callbacks parameter is optional and we always pass nullptr to it, as 
	//you can see in the VDeleter definition.

	// When the wrapped object goes out of scope, the destructor is invoked, which in 
	//turn calls the cleanup function we specified.
	~VDeleter() {
		cleanup();
	}

	// Any extra arguments that are needed for the deleter functions must also be 
	//passed, usually the parent object. It overloads the address-of, assignment, 
	//comparison and casting operators to make the wrapper as transparent as possible:

	// The address-of operator returns a constant pointer to make sure that the object 
	//within the wrapper is not unexpectedly changed...
	const T* operator &() const {
		return &object;
	}

	// ... if you want to replace the handle within the wrapper through a pointer, 
	//then you should use the replace() function instead. It will invoke the cleanup 
	//function for the existing handle so that you can safely overwrite it afterwards.
	T* replace() {
		cleanup();
		return &object;
	}

	operator T() const {
		return object;
	}

	void operator=(T rhs) {
		if (rhs != object) {
			cleanup();
			object = rhs;
		}
	}

	template<typename V>
	bool operator==(V rhs) {
		return object == T(rhs);
	}

private:
	//All of the constructors initialize the object handle with the equivalent of 
	//nullptr in Vulkan: VK_NULL_HANDLE
	T object{ VK_NULL_HANDLE };

	std::function<void(T)> deleter;

	void cleanup() {
		if (object != VK_NULL_HANDLE) {
			deleter(object);
		}
		object = VK_NULL_HANDLE;
	}
};

/*
The program itself is wrapped into a class where we'll store the Vulkan objects as
private class members and add functions to initiate each of them, which will be 
called from the initVulkan function
*/
class HelloTriangleApplication {
public:
	void run() {
		initWindow();
		initVulkan();
		mainLoop();
	}

private:
	GLFWwindow* window;

	VDeleter<VkInstance> instance{ vkDestroyInstance };

	// the debug callback in Vulkan is managed with a handle that needs 
	//to be explicitly created and destroyed
	VDeleter<VkDebugReportCallbackEXT> callback{ instance, DestroyDebugReportCallbackEXT };

	void initWindow() {
		// initializes the GLFW library
		glfwInit();
		// we need to tell it to not create an OpenGL context
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		// handling resized windows takes special care, disable it for now 
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		// The first three parameters specify the width, height and title of the window. 
		// The fourth parameter allows you to optionally specify a monitor to open the 
		//window on and the last parameter is only relevant to OpenGL.
		window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan", nullptr, nullptr);
	}

	void initVulkan() {
		createInstance();
		setupDebugCallback();
	}

	//Rendering loop that iterates until the window is closed in a moment.
	void mainLoop() {
		while (!glfwWindowShouldClose(window)) {
			glfwPollEvents();
		}

		glfwDestroyWindow(window);

		glfwTerminate();
	}

	void createInstance() {
		//Details about the Vulkan support. Optional code for extra info
		#ifdef NDEBUG
		#else
		{//Inside block scope to isolate variables
			uint32_t extensionCount = 0;
			// request just the number of extensions
			vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
			// allocate an array to hold the extension details
			std::vector<VkExtensionProperties> extensions(extensionCount);
			// query the extension details
			vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());
			// list extensions
			std::cout << "available extensions:" << std::endl;

			for (const auto& extension : extensions) {
				std::cout << "\t" << extension.extensionName << std::endl;
			}
		}
		#endif

		// Checking for debug layer support
		if (enableValidationLayers && !checkValidationLayerSupport()) {
			throw std::runtime_error("validation layers requested, but not available!");
		}

		// technically optional, but it may provide some useful information to the driver 
		//to optimize for our specific application
		// https://www.khronos.org/registry/vulkan/specs/1.0/man/html/VkApplicationInfo.html
		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pNext = nullptr;
		appInfo.pApplicationName = "Hello Triangle";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "No Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		// Not optional and tells the Vulkan driver which global extensions and validation layers 
		//we want to use. Global here means that they apply to the entire program and not a 
		//specific device
		// https://www.khronos.org/registry/vulkan/specs/1.0/man/html/VkInstanceCreateInfo.html
		VkInstanceCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		// The next two layers specify the desired global extensions.
		// Vulkan is a platform agnostic API, which means that you need an extension to interface 
		//with the window system and validation layers
		auto extensions = getRequiredExtensions();
		createInfo.enabledExtensionCount = extensions.size();
		createInfo.ppEnabledExtensionNames = extensions.data();


		// The last two members of the struct determine the global validation layers to enable
		if (enableValidationLayers) {
			createInfo.enabledLayerCount = validationLayers.size();
			createInfo.ppEnabledLayerNames = validationLayers.data();
		}
		else {
			createInfo.enabledLayerCount = 0;
		}

		// Create the instance! (checking for errors)
		// https://www.khronos.org/registry/vulkan/specs/1.0/man/html/vkCreateInstance.html
		if (vkCreateInstance(&createInfo, nullptr, instance.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to create instance!");
		}
	}

	// Check whether validation layers are supported
	bool checkValidationLayerSupport() {
		// https://www.khronos.org/registry/vulkan/specs/1.0/man/html/vkEnumerateInstanceLayerProperties.html
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const char* layerName : validationLayers) {
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers) {
				if (strcmp(layerName, layerProperties.layerName) == 0) {
					layerFound = true;
					break;
				}
			}

			if (!layerFound) {
				return false;
			}
		}

		return true;
	}

	// Returns the required list of extensions based on whether validation 
	//layers are enabled or not
	std::vector<const char*> getRequiredExtensions() {
		std::vector<const char*> extensions;

		// GLFW has a handy built-in function that returns the extension(s) it needs to do
		// The extensions specified by GLFW are always required...
		unsigned int glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		for (unsigned int i = 0; i < glfwExtensionCount; i++) {
			extensions.push_back(glfwExtensions[i]);
		}

		//...  but the debug report extension is conditionally added
		if (enableValidationLayers) {
			//VK_EXT_DEBUG_REPORT_EXTENSION_NAME macro here which is 
			//equal to the literal string "VK_EXT_debug_report". 
			//Using this macro lets you avoid typos.
			extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		}

		return extensions;
	}

	// Callback function. The VKAPI_ATTR and VKAPI_CALL ensure that the function 
	//has the right signature for Vulkan to call it.
	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugReportFlagsEXT flags,
		VkDebugReportObjectTypeEXT objType,
		uint64_t obj,
		size_t location,
		int32_t code,
		const char* layerPrefix,
		const char* msg,
		void* userData) {

		std::cerr << "validation layer: " << msg << std::endl;

		return VK_FALSE;
	}

	void setupDebugCallback() {
		if (!enableValidationLayers) return;
		//  structure with details about the callback
		VkDebugReportCallbackCreateInfoEXT createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
		createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
		createInfo.pfnCallback = debugCallback;

		if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, callback.replace()) != VK_SUCCESS) {
			throw std::runtime_error("failed to set up debug callback!");
		}
	}
};

int main() {
	HelloTriangleApplication app;

	try {
		app.run();
	}
	catch (const std::runtime_error& e) {
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}