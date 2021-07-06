// Dear ImGui: standalone example application for GLFW + OpenGL2, using legacy fixed pipeline
// (GLFW is a cross-platform general purpose library for handling windows, inputs, OpenGL/Vulkan/Metal graphics context creation, etc.)
// If you are new to Dear ImGui, read documentation from the docs/ folder + read the top of imgui.cpp.
// Read online: https://github.com/ocornut/imgui/tree/master/docs

// **DO NOT USE THIS CODE IF YOUR CODE/ENGINE IS USING MODERN OPENGL (SHADERS, VBO, VAO, etc.)**
// **Prefer using the code in the example_glfw_opengl2/ folder**
// See imgui_impl_glfw.cpp for details.

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define eprintf(str, ...)                                                       \
    {                                                                           \
        fprintf(stderr, "%s,%d: " str "\n", __func__, __LINE__, ##__VA_ARGS__); \
        fflush(stderr);                                                         \
    }

int connect_w_tout(int soc, const struct sockaddr *addr, socklen_t sock_sz, int tout_s)
{
    int res;
    long arg;
    fd_set myset;
    struct timeval tv;
    int valopt;
    socklen_t lon;

    // Set non-blocking
    if ((arg = fcntl(soc, F_GETFL, NULL)) < 0)
    {
        fprintf(stderr, "Error fcntl(..., F_GETFL) (%s)\n", strerror(errno));
        return -1;
    }
    arg |= O_NONBLOCK;
    if (fcntl(soc, F_SETFL, arg) < 0)
    {
        fprintf(stderr, "Error fcntl(..., F_SETFL) (%s)\n", strerror(errno));
        return -1;
    }
    // Trying to connect with timeout
    res = connect(soc, addr, sock_sz);
    if (res < 0)
    {
        if (errno == EINPROGRESS)
        {
            fprintf(stderr, "EINPROGRESS in connect() - selecting\n");
            do
            {
                if (tout_s > 0)
                    tv.tv_sec = tout_s;
                else
                    tv.tv_sec = 1; // minimum 1 s
                tv.tv_usec = 0;
                FD_ZERO(&myset);
                FD_SET(soc, &myset);
                res = select(soc + 1, NULL, &myset, NULL, &tv);
                if (res < 0 && errno != EINTR)
                {
                    fprintf(stderr, "Error connecting %d - %s\n", errno, strerror(errno));
                    return -1;
                }
                else if (res > 0)
                {
                    // Socket selected for write
                    lon = sizeof(int);
                    if (getsockopt(soc, SOL_SOCKET, SO_ERROR, (void *)(&valopt), &lon) < 0)
                    {
                        fprintf(stderr, "Error in getsockopt() %d - %s\n", errno, strerror(errno));
                        return -1;
                    }
                    // Check the value returned...
                    if (valopt)
                    {
                        fprintf(stderr, "Error in delayed connection() %d - %s\n", valopt, strerror(valopt));
                        return -1;
                    }
                    break;
                }
                else
                {
                    fprintf(stderr, "Timeout in select() - Cancelling!\n");
                    return -1;
                }
            } while (1);
        }
        else
        {
            fprintf(stderr, "Error connecting %d - %s\n", errno, strerror(errno));
            return -1;
        }
    }
    // Set to blocking mode again...
    if ((arg = fcntl(soc, F_GETFL, NULL)) < 0)
    {
        fprintf(stderr, "Error fcntl(..., F_GETFL) (%s)\n", strerror(errno));
        return -1;
    }
    arg &= (~O_NONBLOCK);
    if (fcntl(soc, F_SETFL, arg) < 0)
    {
        fprintf(stderr, "Error fcntl(..., F_SETFL) (%s)\n", strerror(errno));
        return -1;
    }
    // I hope that is all
    return soc;
}

volatile sig_atomic_t done = 0;

void sighandler(int sig)
{
    done = 1;
}

#include "imgui/imgui.h"
#include "backend/imgui_impl_glfw.h"
#include "backend/imgui_impl_opengl2.h"
#include <stdio.h>
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

static void glfw_error_callback(int error, const char *description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

void InitTexture(GLuint &image_texture)
{
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return;
}

void AssignTexture(GLuint &image_texture, unsigned char *data, unsigned image_width, unsigned image_height)
{
    glBindTexture(GL_TEXTURE_2D, image_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width, image_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    return;
}

#include <jpeglib.h>

pthread_mutex_t texture_lock;

GLuint my_image_texture;
int my_image_width, my_image_height;

typedef struct
{
    unsigned char *data;
    unsigned max_size;
    unsigned width;
    unsigned height;
} imagedata;

bool LoadTextureFromMem(const unsigned char *in_jpeg, ssize_t len, imagedata *image)
{
    if (len <= 0 || in_jpeg == NULL || image->data == NULL || image->max_size == 0)
    {
        return false;
    }
    // Load from file
    int image_width = 0;
    int image_height = 0;
    unsigned char *image_data = image->data;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    /* More stuff */
    JSAMPARRAY buffer; /* Output row buffer */
    int row_stride;    /* physical row width in output buffer */

    /* In this example we want to open the input file before doing anything else,
   * so that the setjmp() error recovery below can assume the file is open.
   * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
   * requires it in order to read binary files.
   */
    cinfo.err = jpeg_std_error(&jerr);
    /* Step 1: allocate and initialize JPEG decompression object */
    jpeg_create_decompress(&cinfo);
    /* Step 2: specify data source (eg, a file) */

    jpeg_mem_src(&cinfo, in_jpeg, len);
    /* Step 3: read file parameters with jpeg_read_header() */

    jpeg_read_header(&cinfo, TRUE);
    /* We can ignore the return value from jpeg_read_header since
   *   (a) suspension is not possible with the stdio data source, and
   *   (b) we passed TRUE to reject a tables-only JPEG file as an error.
   * See libjpeg.txt for more info.
   */
    /* Step 4: set parameters for decompression */

    /* In this example, we don't need to change any of the defaults set by
   * jpeg_read_header(), so we do nothing here.
   */

    /* Step 5: Start decompressor */
    cinfo.out_color_space = JCS_EXT_RGBA;
    // cinfo.scale_num = 640; // scale to 480p
    // cinfo.scale_denom = cinfo.image_width;
    (void)jpeg_start_decompress(&cinfo);
    /* We may need to do some setup of our own at this point before reading
   * the data.  After jpeg_start_decompress() we have the correct scaled
   * output image dimensions available, as well as the output colormap
   * if we asked for color quantization.
   * In this example, we need to make an output work buffer of the right size.
   */
    /* JSAMPLEs per row in output buffer */
    row_stride = cinfo.output_width * cinfo.output_components;
    /* Make a one-row-high sample array that will go away when done with image */
    buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr)&cinfo, JPOOL_IMAGE, row_stride, 1);

    /* Step 6: while (scan lines remain to be read) */
    /*           jpeg_read_scanlines(...); */

    /* Here we use the library's state variable cinfo.output_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   */
    int loc = 0;
    if (image->max_size < row_stride * cinfo.output_height)
    {
        printf("%s: Required memory for raw image: %u, allocated: %u\n", __func__, row_stride * cinfo.output_height, image->max_size);
        goto jpeg_end;
    }
    image_height = cinfo.output_height;
    image_width = cinfo.output_width;
    while (cinfo.output_scanline < cinfo.output_height)
    {
        /* jpeg_read_scanlines expects an array of pointers to scanlines.
     * Here the array is only one element long, but you could ask for
     * more than one scanline at a time if that's more convenient.
     */
        (void)jpeg_read_scanlines(&cinfo, buffer, 1);
        /* Assume put_scanline_someplace wants a pointer and sample count. */
        memcpy(&(image_data[loc]), buffer[0], row_stride);
        loc += row_stride;
    }

    /* Step 7: Finish decompression */
jpeg_end:
    (void)jpeg_finish_decompress(&cinfo);
    /* We can ignore the return value since suspension is not possible
   * with the stdio data source.
   */

    /* Step 8: Release JPEG decompression object */

    /* This is an important step since it will release a good deal of memory. */
    jpeg_destroy_decompress(&cinfo);
    /* After finish_decompress, we can close the input file.
   * Here we postpone it until after no more JPEG errors are possible,
   * so as to simplify the setjmp error logic above.  (Actually, I don't
   * think that jpeg_destroy can do an error exit, but why assume anything...)
   */

    image->height = image_height;
    image->width = image_width;
    return true;
}

volatile bool conn_rdy = false;

typedef struct __attribute__((packed))
{
    unsigned width;
    unsigned height;
    float temp;
    float exposure;
    uint64_t tstamp;
    char exposing;
    char num_exposures;
    char curr_exposure;
    char jpeg_quality;
    int size;
} net_meta;

typedef struct __attribute__((packed))
{
    char jpeg_quality;   // JPEG quality
    char start_exposure; // start exposure command
    char stop_exposure;  // stop exposure command
    char num_exposures;  // number of exposures
    char binning;        // binning
    double exposure;     // exposure time
    char prefix[10];     // output prefix
} net_cmd;

typedef struct
{
    net_meta *metadata;
    pthread_mutex_t lock;
    unsigned char *data;
} net_image;

net_image img;
net_cmd atikcmd[1];

static char rcv_buf[1024 * 1024];

char *find_match(char *buf1, ssize_t len1, char *buf2, ssize_t len2)
{
    // fprintf(stderr, "%s: ", __func__);
    // for (int i = 0; i < len2; i++)
    //     fprintf(stderr, "%c", buf2[i]);
    // fprintf(stderr, "\n");
    bool matched = false;
    ssize_t i = 0;
    do
    {
        if (buf1[i] == buf2[0])
        {
            matched = true;
            for (ssize_t j = 1; j < len2; j++)
            {
                // fprintf(stderr, "%s: %c %c\n", __func__, buf1[i + j], buf2[j]);
                if (buf1[i + j] != buf2[j])
                {
                    matched = false;
                    break;
                }
            }
        }
        if (++i >= len1)
            break;
    } while (!matched);
    i--;
    // fprintf(stderr, "%s: matched: %d, idx: %ld, string: ", __func__, matched, i);
    // for (ssize_t j = 0; j < len2; j++)
    //     fprintf(stderr, "%c", buf1[i+j]);
    // fprintf(stderr, "\n");
    if (matched)
        return &(buf1[i]);
    return NULL;
}

pthread_mutex_t lock;
void *rcv_thr(void *sock)
{
    img.metadata = (net_meta *)malloc(sizeof(net_meta));
    img.data = (unsigned char *)malloc(1024 * 1024 * 4);
    memset(rcv_buf, 0x0, sizeof(rcv_buf));
    while (!done)
    {
        memset(rcv_buf, 0x0, sizeof(rcv_buf));
        usleep(1000 * 1000 / 30); // receive at 120 Hz
        if (conn_rdy)
        {
            int offset = 0;
            char *head = NULL, *tail = NULL;
            // retrieve SIZE
            do
            {
                int sz = recv(*(int *)sock, rcv_buf, 1, 0);
                if (sz <= 0)
                    continue;
                if (rcv_buf[0] == 'S')
                {
                    offset = 1;
                    do
                    {
                        offset += recv(*(int *)sock, rcv_buf + offset, 3, 0); // receive SIZE
                    } while ((offset < 4) && conn_rdy);
                }
                if ((rcv_buf[0] == 'S') && (rcv_buf[1] == 'I') && (rcv_buf[2] == 'Z') && (rcv_buf[3] == 'E'))
                    break;
            } while (conn_rdy);
            // read size of buffer
            int payload_sz = 0;
            offset = 0;
            do
            {
                char *psz = (char *)&payload_sz;
                offset += recv(*(int *)sock, psz + offset, sizeof(int), 0);
            } while ((offset < 4) && conn_rdy);
            // now we have payload size
            eprintf("Payload size: %d", payload_sz);
            offset = 0;
            do
            {
                int sz = recv(*(int *)sock, rcv_buf + offset, payload_sz - offset, 0);
                if (sz < 0)
                    continue;
                offset += sz;
                head = find_match(rcv_buf, sizeof(rcv_buf), (char *)"FBEGIN", 6);
                if (head != NULL)
                    tail = find_match(head, sizeof(rcv_buf) - (head - rcv_buf), (char *)"FEND", 4);
                // fprintf(stderr, "received: total %d bytes, head: %p, tail: %p\n", offset, (void *)(head), (void *)(tail));
                // for (int i = 0; i < offset; i++)
                //     fprintf(stderr, "%c", rcv_buf[i]);
                // fprintf(stderr, "\n");
                if (head != NULL && tail != NULL)
                    break;
            } while (conn_rdy);
            if (head != NULL)
            {
                // fprintf(stderr, "%s: Head found at 0x%p\n", __func__, (void *)head);
                if (tail != NULL && tail > head)
                {
                    // fprintf(stderr, "%s: Tail found at 0x%p\n", __func__, (void *)tail);
                    pthread_mutex_lock(&lock);
                    memcpy(img.metadata, head + 6, sizeof(net_meta));
                    fprintf(stderr, "Tstamp: %lu\n", img.metadata->tstamp);
                    fprintf(stderr, "Width: %u\n", img.metadata->width);
                    fprintf(stderr, "Hidth: %u\n", img.metadata->height);
                    fprintf(stderr, "Temp: %f\n", img.metadata->temp);
                    fprintf(stderr, "Exposure: %f s\n", img.metadata->exposure);
                    fprintf(stderr, "JPEG Size: %d\n", img.metadata->size);
                    if (img.metadata->size > 0)
                    {
                        memcpy(img.data, head + 6 + sizeof(net_meta), img.metadata->size);
                    }
                    pthread_mutex_unlock(&lock);
                    if (head + 6 + sizeof(net_meta) + img.metadata->size != tail)
                    {
                        fprintf(stderr, "Head + data does not match tail: 0x%p and 0x%p\n", (void *)(head + 6 + sizeof(net_meta) + img.metadata->size), (void *)tail);
                    }
                }
            }
        }
    }
    free(img.metadata);
    free(img.data);
    return NULL;
}

int main(int, char **)
{
    // setup signal handler
    signal(SIGINT, sighandler);
    signal(SIGPIPE, SIG_IGN); // so that client does not die when server does
    // set up client socket etc
    int sock = -1, valread;
    struct sockaddr_in serv_addr;

    serv_addr.sin_family = AF_INET;

    // we will have to add a port

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;
    GLFWwindow *window = glfwCreateWindow(1280, 720, "Client Example", NULL, NULL);
    if (window == NULL)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;   // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle &style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    ImFont *font = io.Fonts->AddFontFromFileTTF("./imgui/font/Roboto-Medium.ttf", 16.0f);
    if (font == NULL)
        io.Fonts->AddFontDefault();
    //IM_ASSERT(font != NULL);

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    bool show_readout_win = true;

    static int jpg_qty = 70;

    pthread_t rcv_thread;
    int rc = pthread_create(&rcv_thread, NULL, rcv_thr, (void *)&sock);
    if (rc < 0)
    {
        fprintf(stderr, "main: Could not create receiver thread! Exiting...\n");
        goto end;
    }
    // Create a OpenGL texture identifier
    InitTexture(my_image_texture);
    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL2_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
        {
            static int port = 12395;
            static char ipaddr[16] = "192.168.1.18";
            auto flag = ImGuiInputTextFlags_ReadOnly;
            if (!conn_rdy)
                flag = (ImGuiInputTextFlags_)0;

            ImGui::Begin("Connection Manager"); // Create a window called "Hello, world!" and append into it.

            ImGui::InputText("IP Address", ipaddr, sizeof(ipaddr), flag);
            ImGui::InputInt("Port", &port, 0, 0, flag);

            if (!conn_rdy || sock < 0)
            {
                if (ImGui::Button("Connect"))
                {
                    serv_addr.sin_port = htons(port);
                    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
                    {
                        printf("\n Socket creation error \n");
                        fflush(stdout);
                        // return -1;
                    }
                    // else
                    //     fcntl(sock, F_SETFL, O_NONBLOCK); // set the socket non-blocking on macOS
                    if (inet_pton(AF_INET, ipaddr, &serv_addr.sin_addr) <= 0)
                    {
                        printf("\nInvalid address/ Address not supported \n");
                    }
                    // if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
                    if (connect_w_tout(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr), 1) < 0)
                    {
                        printf("\nConnection Failed \n");
                    }
                    else
                    {
                        conn_rdy = true;
                    }
                }
            }
            else
            {
                if (ImGui::Button("Disconnect"))
                {
                    close(sock);
                    sock = -1;
                    conn_rdy = false;
                }
            }
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            bool send_cmd = false;
            static int binning = 1;
            bool start_exposure = false;
            static bool stop_exposure = false;
            if (conn_rdy && sock > 0)
            {
                if (ImGui::InputInt("JPEG Quality", &jpg_qty, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    send_cmd = true;
                }
                if (ImGui::InputInt("Binning", &binning, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue))
                {
                    send_cmd = true;
                    if (binning < 0)
                        binning = 1;
                    if (binning > 4)
                        binning = 4;
                }
                static char exposure_name[10] = "test";
                static int num_exposures = 10;
                ImGui::InputText("Exposure Name", exposure_name, 10, ImGuiInputTextFlags_None);
                if (ImGui::InputInt("Number of exposures", &num_exposures, 0, 0, ImGuiInputTextFlags_None))
                {
                    if (num_exposures < 1)
                        num_exposures = 1;
                    if (num_exposures > 127)
                        num_exposures = 127;
                }
                if (ImGui::Button("Start Exposure"))
                {
                    start_exposure = true;
                }
                if (send_cmd)
                {
                    memset(atikcmd, 0x0, sizeof(net_cmd));
                    atikcmd->jpeg_quality = jpg_qty;
                    atikcmd->binning = binning;
                    atikcmd->num_exposures = num_exposures;
                    if (start_exposure)
                        atikcmd->start_exposure = 1;
                    if (stop_exposure)
                    {
                        atikcmd->stop_exposure = 1;
                        stop_exposure = false;
                    }
                    memcpy(atikcmd->prefix, exposure_name, 10);
                    send(sock, atikcmd, sizeof(net_cmd), 0);
                    send_cmd = false;
                }
            }
            if (conn_rdy && sock > 0)
            {
                pthread_mutex_lock(&texture_lock);
                struct timeval tstamp;
                tstamp.tv_sec = img.metadata->tstamp / (uint64_t)1000000;
                tstamp.tv_usec = (img.metadata->tstamp % 1000000);
                struct tm ts;
                char buf[80];

                // Format time, "ddd yyyy-mm-dd hh:mm:ss zzz"
                ts = *localtime(&tstamp.tv_sec);
                strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S %Z", &ts);
                ImGui::Text(u8"Timestamp: %s | Size: %d x %d | Exposure: %.3f s | CCD Temp: %.2f °C", buf, img.metadata->width, img.metadata->height, img.metadata->exposure, img.metadata->temp);
                ImGui::Text(u8"Recording exposures: %s | On exposure: %d | Total Exposures: %d", img.metadata->exposing ? "YES" : "NO ", img.metadata->curr_exposure, img.metadata->num_exposures);
                if (ImGui::Button("Stop Exposure"))
                {
                    if (img.metadata->exposing)
                    {
                        stop_exposure = true;
                    }
                }
                jpg_qty = img.metadata->jpeg_quality;
                imagedata live_image;
                live_image.max_size = 1024 * 1024 * 4 * 2;
                live_image.data = (unsigned char *)malloc(live_image.max_size);
                LoadTextureFromMem(img.data, (img.metadata->size), &live_image);
                float w = ImGui::GetContentRegionAvailWidth();
                float h = w * (live_image.height * 1.0 / live_image.width);
                AssignTexture(my_image_texture, live_image.data, live_image.width, live_image.height);
                free(live_image.data);
                ImGui::Image((void *)(intptr_t)my_image_texture, ImVec2(w, h));
                pthread_mutex_unlock(&texture_lock);
            }
            ImGui::End();
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);

        // If you are using this code with non-legacy OpenGL header/contexts (which you should not, prefer using imgui_impl_opengl3.cpp!!),
        // you may need to backup/reset/restore current shader using the commented lines below.
        //GLint last_program;
        //glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
        //glUseProgram(0);
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
        //glUseProgram(last_program);

        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow *backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);
    }
end:
    done = 1;
    close(sock);
    // Cleanup
    ImGui_ImplOpenGL2_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
