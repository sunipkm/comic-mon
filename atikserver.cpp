// Server side C/C++ program to demonstrate Socket programming
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <gpiodev/gpiodev.h>

#include <iostream>
#include <fitsio.h>
#include <signal.h>

#include <atikccdusb.h>

#ifdef __cplusplus
extern "C"
{
#endif
#include <jpeglib.h>
#ifdef __cplusplus
}
#endif

#ifndef SO_REUSEPORT
#define SO_REUSEPORT 15
#endif

#define eprintf(str, ...)                                                        \
    {                                                                            \
        fprintf(stderr, "%s, %d: " str "\n", __func__, __LINE__, ##__VA_ARGS__); \
        fflush(stderr);                                                          \
    }

using namespace std;

#define TIME_NSEC (uint64_t)1000000000
#define TIME_USEC (uint64_t)1000000
/**
 * @brief Class to obtain current system time
 * 
 */
class systime
{
public:
    struct timeval ts;
    /**
     * @brief Return timestamp to microsecond (nearest)
     * 
     * @return long long int timestamp in microseconds
     */
    uint64_t usec()
    {
        return this->ts.tv_sec * TIME_USEC + this->ts.tv_usec;
    }
    systime operator-(struct systime &ts2)
    {
        struct systime tmp;
        tmp.ts.tv_sec = this->ts.tv_sec - ts2.ts.tv_sec;
        tmp.ts.tv_usec = this->ts.tv_usec - ts2.ts.tv_usec;
        return tmp;
    }
    void operator-=(struct systime &ts2)
    {
        struct systime tmp;
        this->ts.tv_sec -= ts2.ts.tv_sec;
        this->ts.tv_usec -= ts2.ts.tv_usec;
    }
    /**
     * @brief Store the current timestamp
     * 
     */
    void now()
    {
        gettimeofday(&(this->ts), NULL);
    }
    /**
     * @brief Construct a new systime object, and store current time
     * 
     */
    systime()
    {
        gettimeofday(&(this->ts), NULL);
    }
};

/**
 * @brief Class to store average and standard deviation of a data series
 * 
 */
class statseries
{
public:
    /**
     * @brief Average of the series
     * 
     */
    double avg = 0;
    /**
     * @brief Standard deviation (sigma) of the series
     * 
     */
    double stdev = 0;
    /**
     * @brief Length of the series
     * 
     */
    unsigned long long int n = 0;
    /**
     * @brief Add new point to the series
     * 
     * @param val Data point
     */
    void add(double val)
    {
        double avg = this->avg;
        double stdev = this->stdev;
        unsigned long long int n = this->n;
        avg = (avg * n) + val;
        stdev = (stdev * stdev * n) + val * val;
        n += 1;
        avg /= n;
        stdev = sqrt(stdev / n);
        this->avg = avg;
        this->stdev = stdev;
        this->n = n;
    }
};

typedef struct
{
    unsigned short *data;
    unsigned width;
    unsigned height;
    float temp;
    float exposure;
    unsigned long long tstamp;
} comic_image;

class jpeg_image
{
private:
    unsigned char *data;
    int sz;

public:
    static int jpeg_quality;
    void convert_jpeg_image(unsigned short *data, unsigned width, unsigned height)
    {
        // convert grayscale data first
        unsigned char *gr_data = (unsigned char *)malloc(width * height);
        this->data = (unsigned char *)malloc(1024 * 1024 * 4); // allocate 4M
        for (unsigned long long i = 0; i < width * height; i++)
        {
            gr_data[i] = data[i] / 256; // convert to 8 bit grayscale
        }
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        JSAMPROW row_pointer[1]; /* line pointer */
        int row_stride;          /* Row span (how many bytes are needed for a row in the image) */
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);
        unsigned long img_sz = 0;
        jpeg_mem_dest(&cinfo, &(this->data), &(img_sz));
        cinfo.image_width = width;
        cinfo.image_height = height;
        cinfo.scale_denom = 1;
        cinfo.scale_num = 1;
        cinfo.input_components = 1;
        cinfo.in_color_space = JCS_GRAYSCALE;
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, jpeg_quality, TRUE);
        jpeg_start_compress(&cinfo, TRUE);
        row_stride = width; // unsigned char
        while (cinfo.next_scanline < height)
        {
            row_pointer[0] = &(gr_data[cinfo.next_scanline * row_stride]);
            (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
        }
        jpeg_finish_compress(&cinfo);
        jpeg_destroy_compress(&cinfo);
        free(gr_data);
        this->sz = (int)img_sz;
    }
    int copy_image(unsigned char *buf)
    {
#ifdef TEST_JPEG_IMG
        static int imgnum = 0;
        char fname[30];
        snprintf(fname, 30, "testimg/i%d.jpg", imgnum++);
        unlink(fname);
        FILE *fp = fopen(fname, "wb");
        if (this->sz > 0)
            fwrite(this->data, 1, this->sz, fp);
        fclose(fp);
#endif //TEST_JPEG_IMG
        if (this->sz > 0)
            memcpy(buf, this->data, this->sz);
        free(this->data);
        return this->sz;
    }
    static void set_jpeg_quality(int q)
    {
        if (q < 0)
            q = 70;
        else if (q > 100)
            q = 100;
        jpeg_quality = q;
    }
};

int jpeg_image::jpeg_quality = 70;

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
    char binning;
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

pthread_mutex_t net_img_lock;

typedef struct
{
    net_meta *metadata;
    pthread_mutex_t lock;
    unsigned char *data;
} net_image;

bool checkSaturation(unsigned short *img, unsigned int size)
{
    int count = size * 0.9; // 90% pixels are not saturated
    for (unsigned int i = 0; i < size; i++)
    {
        if (img[i] == 65535) // saturated
            count--;         // reduce counts from 90%
    }
    if (count < 0) // more than 90% pixels are saturated
        return true;
    return false;
}

bool checkDark(unsigned short *img, unsigned int size)
{
    int count = size * 0.3; // 30% pixels are not dark
    for (unsigned int i = 0; i < size; i++)
    {
        if (img[i] < 2000) // dark
            count--;       // reduce counts from 30%
    }
    if (count < 0) // more than 30% pixels are dark
        return true;
    return false;
}

/* Sorting */
int compare(const void *a, const void *b)
{
    return (*((unsigned short *)a) - *((unsigned short *)b));
}

#define MAX_ALLOWED_EXPOSURE 10.0 // 10 seconds

double find_optimum_exposure(unsigned short *picdata, unsigned int imgsize, double exposure)
{
//#define SK_DEBUG
#ifdef SK_DEBUG
    cerr << __FUNCTION__ << " : Received exposure: " << exposure << endl;
#endif
    double result = exposure;
    double val;
    qsort(picdata, imgsize, sizeof(unsigned short), compare);

#ifdef MEDIAN
    if (imgsize && 0x01)
        val = (picdata[imgsize / 2] + picdata[imgsize / 2 + 1]) * 0.5;
    else
        val = picdata[imgsize / 2];
#endif //MEDIAN

#ifndef MEDIAN
#ifndef PERCENTILE
#define PERCENTILE 90.0
    bool direction;
    if (picdata[0] < picdata[imgsize - 1])
        direction = true;
    else
        direction = false;

    unsigned int coord = floor((PERCENTILE * (imgsize - 1) / 100.0));
    if (direction)
        val = picdata[coord];
    else
    {
        if (coord == 0)
            coord = 1;
        val = picdata[imgsize - coord];
    }

#ifdef SK_DEBUG
    cerr << "Info: " << __FUNCTION__ << "Direction: " << direction << ", Coordinate: " << coord << endl;
    cerr << "Info: " << __FUNCTION__ << "10 values around the coordinate: " << endl;
    unsigned int lim2 = imgsize - coord > 3 ? coord + 4 : imgsize - 1;
    unsigned int lim1 = lim2 - 10;
    for (int i = lim1; i < lim2; i++)
        cerr << picdata[i] << " ";
    cerr << endl;
#endif

#endif //PERCENTILE
#endif //MEDIAN
#ifdef SK_DEBUG
    cerr << "In " << __FUNCTION__ << ": Median: " << val << endl;
#endif
#ifndef PIX_MEDIAN
#define PIX_MEDIAN 40000.0
#endif

#ifndef PIX_GIVE
#define PIX_GIVE 5000.0
#endif

    if (val > PIX_MEDIAN - PIX_GIVE && val < PIX_MEDIAN + PIX_GIVE /* && PIX_MEDIAN - PIX_GIVE > 0 && PIX_MEDIAN + PIX_GIVE < 65535 */)
        return result;

    /** If calculated median pixel is within PIX_MEDIAN +/- PIX_GIVE, return current exposure **/

    result = ((double)PIX_MEDIAN) * exposure / ((double)val);

#ifdef SK_DEBUG
    cerr << __FUNCTION__ << " : Determined exposure from median " << val << ": " << result << endl;
#endif

    if (result > MAX_ALLOWED_EXPOSURE)
        result = MAX_ALLOWED_EXPOSURE;
    // round to 1 ms
    result = ((int)(result * 1000)) / 1000.0;
    return result;
    //#undef SK_DEBUG
}

volatile sig_atomic_t done = 0;
void sig_handler(int in)
{
    done = 1;
    eprintf("%s: Received signal %d\n", __func__, in);
}
#define PORT 12395

net_cmd atikcmd[1];
bool valid_cmd = false;

void *cmd_rcv_fcn(void *sock)
{
    while (!done)
    {
        if (*(int *)sock > 0)
        {
            int offset = 0;
            do
            {
                int sz = recv(*(int *)sock, (char *)atikcmd + offset, sizeof(net_cmd) - offset, MSG_NOSIGNAL);
                if (sz < 0)
                    continue;
                offset += sz;
            } while (!done && (offset < sizeof(net_cmd)));

            if (offset == sizeof(net_cmd)) // valid command
            {
                valid_cmd = true;
                int tmp = atikcmd->jpeg_quality;
                if (tmp > 100)
                    tmp = 100;
                else if (tmp < 0)
                    tmp = 70;
                eprintf("decoded jpeg quality: %d\n", tmp);
                jpeg_image::set_jpeg_quality(tmp);
            }
        }
        usleep(1000000 / 60); // receive a command at 60 Hz
    }
    return NULL;
}

void *cmd_fcn(void *img)
{
    int server_fd, new_socket = -1, valread;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    net_image *jpg = (net_image *)img;
    // Creating socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)))
    {
        perror("setsockopt");
        printf("%s: %d\n", __func__, __LINE__);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT,
                   &opt, sizeof(opt)))
    {
        perror("setsockopt");
        printf("%s: %d\n", __func__, __LINE__);
        fflush(stdout);
        exit(EXIT_FAILURE);
    }
    int flags = fcntl(server_fd, F_GETFL, 0);
    assert(flags != -1);
    fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Forcefully attaching socket to the port 8080
    if (bind(server_fd, (struct sockaddr *)&address,
             sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 3) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    int counter = 0;

    pthread_t rcv_thread;

    int rc = pthread_create(&rcv_thread, NULL, cmd_rcv_fcn, (void *)&new_socket);
    if (rc < 0)
    {
        eprintf("Could not create receive thread, exiting...\b");
        close(new_socket);
        return 0;
    }
    bool conn_rdy = false;
    while (!done)
    {
        // cout << "Img: " << jpg->metadata->size << " bytes, metadata: " << sizeof(net_meta) << " bytes" << endl;
        pthread_mutex_lock(&net_img_lock);
        if (jpg->metadata->size > 0)
        {
            int sz = 0;
            int32_t out_sz = jpg->metadata->size + sizeof(net_meta) + 18; // total size = size of metadata + size of image + FBEGIN + FEND
            unsigned char *buf = (unsigned char *)malloc(out_sz);         // image size + image data area
            memcpy(buf, "SIZE", 4);
            memcpy(buf + 4, &out_sz, 4);
            memcpy(buf + 8, "FBEGIN", 6);                                         // copy FBEGIN
            memcpy(buf + 14, jpg->metadata, sizeof(net_meta));                    // copy metadata
            memcpy(buf + 14 + sizeof(net_meta), jpg->data, jpg->metadata->size);  // copy jpeg data
            memcpy(buf + 14 + sizeof(net_meta) + jpg->metadata->size, "FEND", 4); // copy FEND
            sz = send(new_socket, buf, out_sz, MSG_NOSIGNAL);
            if (sz > 0)
                cout << "Sent: " << sz << " bytes of " << out_sz << " bytes, image: " << jpg->metadata->size << " bytes" << endl;
            if (sz <= 0)
                conn_rdy = false;
            jpg->metadata->size = 0; // indicate data has been sent
            free(buf);
        }
        pthread_mutex_unlock(&net_img_lock);
        // eprintf("%s: Sent %d bytes: %s", __func__, sz, hello);
        if ((!conn_rdy) && (!done))
        {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
                                     (socklen_t *)&addrlen)) < 0)
            {
#ifdef SERVER_DEBUG
                perror("accept");
#endif
            }
            else
                conn_rdy = true;
        }
        usleep(1000000 / 2); // 30 Hz
    }

    pthread_join(rcv_thread, NULL);

    close(new_socket);

    return NULL;
}

typedef struct
{
    uint16_t *data;
    uint32_t width;
    uint32_t height;
    float temp;
    float exposure;
    uint64_t tstamp;
} AtikImage;

class Atik414ex
{
private:
    AtikCamera *devices[1];
    AtikCamera *device;
    AtikCapabilities devcap[1];
    bool devopen;
    unsigned tscount, maxBinX, maxBinY;
    bool longExpMode;
    double exposure;
    systime tnow[1];
    AtikImage img[1];

    bool getPicture()
    {
        bool success = false;
        if (img->data != NULL)
            success = device->getImage((unsigned short *)img->data, img->width * img->height);
        return success;
    }

public:
    unsigned pixelCX, pixelCY, maxBin;
    int offsetX, offsetY;
    double minShortExp, maxShortExp;
    bool open()
    {
        int count = AtikCamera::list(devices, 1);
        if (count < 1)
            return false;
        eprintf("Device count: %d", count);
        device = devices[0];
        devopen = device->open();
        return devopen;
    }

    bool getCap()
    {
        const char *devname;
        CAMERA_TYPE type;
        if (devopen)
        {
            if (device->getCapabilities(&devname, &type, devcap))
            {
                pixelCX = devcap->pixelCountX;
                pixelCY = devcap->pixelCountY;
                maxBinX = devcap->maxBinX;
                maxBinY = devcap->maxBinY;
                tscount = devcap->tempSensorCount;
                offsetX = devcap->offsetX;
                offsetY = devcap->offsetY;
                longExpMode = devcap->supportsLongExposure;
                minShortExp = devcap->minShortExposure;
                maxShortExp = devcap->maxShortExposure;
                exposure = maxShortExp; // startup
                maxBin = maxBinX > maxBinY ? maxBinY : maxBinX;
                img->data = NULL;                            // memset
                img->data = new uint16_t[pixelCX * pixelCY]; // max data
            }
            else
                return false;
            return true;
        }
        return false;
    }

    AtikImage *snapPicture(unsigned ofX, unsigned ofY, unsigned sX, unsigned sY, unsigned bin, double exposure)
    {
        AtikImage *prop = NULL; // start with NULL
        unsigned height = device->imageHeight(sY, bin);
        unsigned width = device->imageWidth(sX, bin);

        if (bin > maxBin)
        {
            return prop; // error
        }
        // snap picture
        bool success = false;
        img->exposure = exposure;
        img->height = height;
        img->width = width;
        tnow->now(); // update time
        if (exposure > maxShortExp)
        {
            success = device->startExposure(false);
            if (!success)
            {
                return NULL;
            }
            long delay = device->delay(exposure);
            usleep(delay);
            success = device->readCCD(ofX, ofY, sX, sY, bin, bin);
        }
        else
            success = device->readCCD(ofX, ofY, sX, sY, bin, bin, exposure);
        // set imgprop properties
        if (success)
        {
            if (getPicture())
                prop = img;
        }
        // get temperature
        img->temp = getTemp(); // get temperature AFTER exposure
        img->tstamp = tnow->usec();
        return prop;
    }

    AtikImage *snapPicture(unsigned ofX, unsigned ofY, unsigned sX, unsigned sY, unsigned bin)
    {
        return snapPicture(ofX, ofY, sX, sY, bin, exposure);
    }

    AtikImage *snapPicture(unsigned bin, double exposure)
    {
        return snapPicture(0, 0, pixelCX, pixelCY, bin, exposure);
    }

    AtikImage *snapPicture(unsigned bin)
    {
        return snapPicture(bin, exposure);
    }

    void setExposure(double exposure)
    {
        this->exposure = exposure;
    }

    double getExposure()
    {
        return this->exposure;
    }

    float getTemp()
    {
        float temp[tscount];
        if (device->getTemperatureSensorStatus(tscount, temp))
            return temp[0];
        return -273.15; // return 0K as error
    }

    unsigned getMaxSize()
    {
        return pixelCX * pixelCY;
    }

    Atik414ex()
    {
        devopen = false;
        if (open())
        {
            if (!getCap())
            {
                throw std::runtime_error("Error getting capabilities");
                goto err;
            }
            sleep(1);
            eprintf("Out of sleep");
            if (snapPicture(1, minShortExp) == NULL)
            {
                throw std::runtime_error("Could not initialize first exposure");
                goto err;
            }
            return;
        }
        else
            throw std::runtime_error("Error opening device");
    err:
        devopen = false;
        device->close();
        return;
    }
    ~Atik414ex()
    {
        if (devopen)
        {
            delete[] img->data;
            device->close();
            devopen = false;
        }
    }

    bool saveFits()
    {
        if (img->tstamp == 0) // not snapped
            return false;
        char fileName[256];
        snprintf(fileName, sizeof(fileName), "fits/%llu.fit[compress]", img->tstamp / 1000);
        fitsfile *fptr;
        int status = 0, bitpix = USHORT_IMG, naxis = 2;
        int bzero = 32768, bscale = 1;
        long naxes[2] = {(long)(img->width), (long)(img->height)};
        unlink(fileName);
        if (!fits_create_file(&fptr, fileName, &status))
        {
            fits_create_img(fptr, bitpix, naxis, naxes, &status);
            fits_write_key(fptr, TSTRING, "PROGRAM", (void *)"atik_ccd_test", NULL, &status);
            fits_write_key(fptr, TUSHORT, "BZERO", &bzero, NULL, &status);
            fits_write_key(fptr, TUSHORT, "BSCALE", &bscale, NULL, &status);
            fits_write_key(fptr, TFLOAT, "SENSOR TEMP", &(img->temp), NULL, &status);
            fits_write_key(fptr, TFLOAT, "EXPOSURE", &(img->exposure), NULL, &status);
            fits_write_key(fptr, TLONGLONG, "TIMESTAMP", &(img->tstamp), NULL, &status);
            long fpixel[] = {1, 1};
            fits_write_pix(fptr, TUSHORT, fpixel, (img->width) * (img->height), img->data, &status);
            fits_close_file(fptr, &status);
            cerr << endl
                 << "saved to " << fileName << endl
                 << endl;
        }
    }

    bool saveFits(const char *prefix)
    {
        if (img->tstamp == 0) // not snapped
            return false;
        char fileName[256];
        if ((prefix != NULL) && (strlen(prefix) > 0))
            snprintf(fileName, sizeof(fileName), "fits/%s_%llu.fit[compress]", prefix, img->tstamp / 1000);
        else
            snprintf(fileName, sizeof(fileName), "fits/%llu.fit[compress]", img->tstamp / 1000);
        fitsfile *fptr;
        int status = 0, bitpix = USHORT_IMG, naxis = 2;
        int bzero = 32768, bscale = 1;
        long naxes[2] = {(long)(img->width), (long)(img->height)};
        unlink(fileName);
        if (!fits_create_file(&fptr, fileName, &status))
        {
            fits_create_img(fptr, bitpix, naxis, naxes, &status);
            fits_write_key(fptr, TSTRING, "PROGRAM", (void *)"atik_ccd_test", NULL, &status);
            fits_write_key(fptr, TUSHORT, "BZERO", &bzero, NULL, &status);
            fits_write_key(fptr, TUSHORT, "BSCALE", &bscale, NULL, &status);
            fits_write_key(fptr, TFLOAT, "SENSOR TEMP", &(img->temp), NULL, &status);
            fits_write_key(fptr, TFLOAT, "EXPOSURE", &(img->exposure), NULL, &status);
            fits_write_key(fptr, TLONGLONG, "TIMESTAMP", &(img->tstamp), NULL, &status);
            long fpixel[] = {1, 1};
            fits_write_pix(fptr, TUSHORT, fpixel, (img->width) * (img->height), img->data, &status);
            fits_close_file(fptr, &status);
            cerr << endl
                 << "saved to " << fileName << endl
                 << endl;
        }
    }
};

int main(int argc, char *argv[])
{
    bool saveFit = true; // mark false if not needed
    char exposing = 0;
    double const_exposure = 0;
    char num_exposures, curr_exposure;
    static int exposure_set = 0;
    int binning = 1, const_binning = 1;
    int file_prefix[10];
    signal(SIGINT, sig_handler);
    gpioSetMode(11, GPIO_OUT);
    gpioWrite(11, GPIO_HIGH);
    sleep(1);
    Atik414ex *device = new Atik414ex();
    double exposure;
    bool success;

    unsigned short tmp[device->getMaxSize()];

    int rc = 0;

    net_image *ext_img = (net_image *)malloc(sizeof(net_image));
    ext_img->data = (unsigned char *)malloc(1024 * 1024 * 4); // 4 MiB

    systime tnow;
    if (ext_img == NULL)
    {
        eprintf("main: Error malloc: ");
        perror("ext_img");
        goto end;
    }
    ext_img->metadata = (net_meta *)malloc(sizeof(net_meta));
    if (ext_img->metadata == NULL)
    {
        eprintf("main: Error malloc: ");
        perror("ext_img->metadata");
        goto end;
    }

    pthread_t cmd_thread;
    rc = pthread_create(&cmd_thread, NULL, &cmd_fcn, (void *)ext_img);
    if (rc != 0)
    {
        eprintf("main: Failed to create comm thread, exiting...");
        goto end;
    }
    while (!done)
    {
        if (valid_cmd)
        {
            binning = atikcmd->binning;
            if (atikcmd->start_exposure && (!exposing))
            {
                const_exposure = atikcmd->exposure;
                if (const_exposure < 0.001)
                    const_exposure = 0.001;
                else if (const_exposure > 10)
                    const_exposure = 10;
                exposing = 1; // indicate we are exposing
                num_exposures = atikcmd->num_exposures;
                curr_exposure = 0;
                atikcmd->start_exposure = 0;
                const_binning = atikcmd->binning;
                if (const_binning < 1)
                    const_binning = 1;
                else if (const_binning > 4)
                    const_binning = 4;
                memcpy(file_prefix, atikcmd->prefix, 10);
                exposure_set++;
            }
            else if (exposing && atikcmd->stop_exposure)
            {
                exposing = 0;
                num_exposures = 0;
                curr_exposure = 0;
                atikcmd->stop_exposure = 0;
            }
            valid_cmd = false;
        }
        AtikImage *props = NULL;
        if (exposing && (curr_exposure < num_exposures))
        {
            props = device->snapPicture(const_binning, const_exposure);
            curr_exposure++;
        }
        else
            props = device->snapPicture(binning, exposure);
        if (curr_exposure == num_exposures)
        {
            exposing = 0;
            num_exposures = 0;
            curr_exposure = 0;
        }
        if (props == NULL)
        {
            eprintf("Props is null");
            // continue;
        }
        cout << "Obtained exposure" << endl;
        jpeg_image img;
        img.convert_jpeg_image(props->data, props->width, props->height);
        cout << "jpeg created" << endl;
        pthread_mutex_lock(&net_img_lock);
        cout << "mutex lock" << endl;
        ext_img->metadata->temp = props->temp;
        cout << "CCD temp: " << ext_img->metadata->temp << " C" << endl;
        ext_img->metadata->tstamp = props->tstamp;
        cout << "Tstamp: " << ext_img->metadata->tstamp << endl;
        ext_img->metadata->height = props->height;
        cout << "Height: " << ext_img->metadata->height << endl;
        ext_img->metadata->width = props->width;
        cout << "Width: " << ext_img->metadata->width << endl;
        ext_img->metadata->exposure = props->exposure;
        cout << "Exposure: " << ext_img->metadata->exposure << endl;
        ext_img->metadata->size = img.copy_image(ext_img->data);
        cout << "Size: " << ext_img->metadata->size << endl;
        ext_img->metadata->jpeg_quality = jpeg_image::jpeg_quality;
        ext_img->metadata->exposing = exposing;
        ext_img->metadata->binning = binning;
        if (exposing)
        {
            ext_img->metadata->curr_exposure = curr_exposure;
            ext_img->metadata->num_exposures = num_exposures;
            ext_img->metadata->binning = const_binning;
            char prefix[100];
            snprintf(prefix, 100, "%s_set%d_%.3lf_%d_%d", file_prefix, exposure_set, const_exposure, curr_exposure, num_exposures);
            device->saveFits(prefix);
        }
        pthread_mutex_unlock(&net_img_lock);
        if (!exposing)
        {
            if (!done)
                exposure = find_optimum_exposure(props->data, props->width * props->height, exposure);
            if (exposure < device->minShortExp)
                exposure = device->minShortExp;
            if (exposure > MAX_ALLOWED_EXPOSURE)
                exposure = MAX_ALLOWED_EXPOSURE;
        }
    }
    cout << "main: Out of loop" << endl
         << flush;
    done = 1;
    sleep(1);
    rc = pthread_cancel(cmd_thread);
end:
    free(ext_img->metadata);
    free(ext_img);
    gpioWrite(11, GPIO_LOW);
    return 0;
}
