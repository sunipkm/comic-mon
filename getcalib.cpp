#include <iostream>
#include <unistd.h>
#include <string.h>
#include <fitsio.h>
#include <signal.h>

#include <atikccdusb.h>

#define MAX 10
#define SIZE 100

using namespace std;

static AtikCamera *devices[MAX];

void save(const char *fileName, unsigned short *data, unsigned width, unsigned height, float temp)
{
    fitsfile *fptr;
    int status = 0, bitpix = USHORT_IMG, naxis = 2;
    int bzero = 32768, bscale = 1;
    long naxes[2] = {(long)width, (long)height};
    unlink(fileName);
    if (!fits_create_file(&fptr, fileName, &status))
    {
        fits_create_img(fptr, bitpix, naxis, naxes, &status);
        fits_write_key(fptr, TSTRING, "PROGRAM", (void *)"atik_ccd_test", NULL, &status);
        fits_write_key(fptr, TUSHORT, "BZERO", &bzero, NULL, &status);
        fits_write_key(fptr, TUSHORT, "BSCALE", &bscale, NULL, &status);
        fits_write_key(fptr, TFLOAT, "SENSOR TEMP", &temp, NULL, &status);
        long fpixel[] = {1, 1};
        fits_write_pix(fptr, TUSHORT, fpixel, width * height, data, &status);
        fits_close_file(fptr, &status);
        cerr << endl
             << "saved to " << fileName << endl
             << endl;
    }
}

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

volatile sig_atomic_t done = 0;

void sighandler(int sig)
{
    done = 1;
}

int main(int argc, char *argv[])
{
    struct sigaction sa;
    sa.sa_handler = &sighandler;
    sigaction(SIGINT, &sa, NULL);
    int count = AtikCamera::list(devices, MAX);
    for (int i = 0; i < count; i++)
    {
        AtikCamera *device = devices[i];
        cout << "open " << device->getName() << endl;

        bool success = device->open();

        if (success)
            cout << "getting capabilities: " << endl;
        AtikCapabilities *devcap = new AtikCapabilities;
        const char *devname;
        CAMERA_TYPE type;
        success = device->getCapabilities(&devname, &type, devcap);

        if (!success)
        {
            cout << "Could not get capabilites" << endl;
            return -1;
        }

        unsigned pixelCX = devcap->pixelCountX;
        unsigned pixelCY = devcap->pixelCountY;

        unsigned pixelSX = devcap->pixelSizeX;
        unsigned pixelSY = devcap->pixelSizeY;

        unsigned maxBinX = devcap->maxBinX;
        unsigned maxBinY = devcap->maxBinY;

        unsigned tempSensorCount = devcap->tempSensorCount;

        int offsetX = devcap->offsetX;
        int offsetY = devcap->offsetY;

        bool longExpMode = devcap->supportsLongExposure;

        double minShortExp = devcap->minShortExposure;
        double maxShortExp = devcap->maxShortExposure;

        unsigned maxPixBin = maxBinX > maxBinY ? maxBinY : maxBinX;

        cout << "Max Pixel Bin: " << maxPixBin << endl;

        maxPixBin = 4;

        unsigned short tmp[pixelCX * pixelCY];

        success = device->readCCD(0, 0, pixelCX, pixelCY, 1, 1, 0.001);
        if (success)
            success = device->getImage(tmp, pixelCX * pixelCY);
        else
        {
            cout << "Could not get first exposure" << endl;
            return -1;
        }

        for (unsigned pixBin = 1; pixBin <= maxPixBin; pixBin *= 2) // bin loop
        {
            unsigned width = device->imageWidth(pixelCX, pixBin);
            unsigned height = device->imageWidth(pixelCY, pixBin);
            unsigned short *picData = new unsigned short[width * height];
            // exposure loop
            for (unsigned expTimeMs = 1; expTimeMs <= maxShortExp * 1000 * 10; expTimeMs *= 5)
            {
                for (int j = 0; j < MAX_IMAGES; j++)
                {
                    if (expTimeMs > maxShortExp * 1000)
                    {
                        success = device->startExposure(false);
                        if (!success || done)
                        {
                            cout << "Failed to start long exposure" << endl;
                            return -1;
                        }
                        long delay = device->delay(expTimeMs * 0.001d);
                        // cout << "Exposure delay: " << delay << " us" << endl;
                        usleep(delay);
                        success = device->readCCD(0, 0, pixelCX, pixelCY, pixBin, pixBin);
                    }
                    else
                        success = device->readCCD(0, 0, pixelCX, pixelCY, pixBin, pixBin, (double)expTimeMs * 0.0010d);
                    if (success && (!done))
                        success = device->getImage(picData, width * height);
                    else
                    {
                        cout << "Error reading CCD" << endl;
                        return -1;
                    }

                    float temp = -70;
                    success = device->getTemperatureSensorStatus(1, &temp);
                    char fname[256];
                    snprintf(fname, 256, "bin%u_exp%u_%d.fit", pixBin, expTimeMs, j);
                    save(fname, picData, width, height, temp);
                    if (checkSaturation(picData, width * height))
                        expTimeMs = maxShortExp * 1000 * 1000; // break the loop
                }
            }
            delete picData;
        }
        delete devcap;
        device->close();
    }
    return 0;
}