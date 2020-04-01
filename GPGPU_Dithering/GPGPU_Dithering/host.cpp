// Add you host code.

#include "deviceInfo.h"
// BMP 이미지를 불러오기 위한 작업
#pragma warning(disable : 4996)

double total_Time_GPU, total_Time_CPU, tmp_time;

BITMAPFILEHEADER bfh;
BITMAPINFOHEADER bih;
RGBQUAD* rgb;

int quant_error;
int bpl, bph, div_bpl, div_bph;

unsigned char* pix;
int * pixE;

unsigned char* pix_output;
int * pixE_output;



char* readSource(char* kernelPath);
void CLInit();
void bufferWrite();
void runKernel();
void Release();

void CpuCal();
void FwriteCPU();
void FwriteGPU();

int main(int argc, char** argv) {

	FILE * fp;
	fp = fopen("EDIMAGE.bmp", "rb");

	fread(&bfh, sizeof(bfh), 1, fp);
	fread(&bih, sizeof(bih), 1, fp);

	rgb = (RGBQUAD*)malloc(sizeof(RGBQUAD) * 256);
	fread(rgb, sizeof(RGBQUAD), 256, fp);

	// BPL을 맞춰주기 위해서 픽셀데이터의 사이즈를 4의 배수로 조정
	bpl = (bih.biWidth + 3) / 4 * 4;
	bph = (bih.biHeight + 3) / 4 * 4;
	// GPU로 연산할때 구간을 분할할 갯수
	div_bpl = bpl / 64;
	div_bph = bpl / 64;

	pix = (unsigned char *)malloc(sizeof(unsigned char) * bpl * bph);
	memset(pix, 0, sizeof(unsigned char) * bpl * bph);
	fread(pix, sizeof(unsigned char), bpl * bph, fp);

	pix_output = (unsigned char *)malloc(sizeof(unsigned char) * bpl * bph);
	memcpy(pix_output, pix, sizeof(unsigned char) * bpl * bph);

	pixE = (int *)malloc(sizeof(int) * bpl * bph);
	memset(pixE, 0, sizeof(int) * bpl * bph);

	pixE_output = (int *)malloc(sizeof(int) * bpl * bph);
	memset(pixE_output, 0, sizeof(int) * bpl * bph);


	// GPU 연산을 위한 측정 시간 초기화.
	total_Time_GPU = 0;
	// GPU 연산
	QueryPerformanceFrequency(&tot_clockFreq);	// 시간을 측정하기위한 준비
	// OpenCL 디바이스, 커널 셋업
	CLInit();


	QueryPerformanceCounter(&tot_beginClock); // 시간측정 시작
	// 디바이스 쪽 버퍼 생성 및 write								 
	bufferWrite();
	//커널 실행
	runKernel();
	QueryPerformanceCounter(&tot_endClock);
	total_Time_GPU = (double)(tot_endClock.QuadPart - tot_beginClock.QuadPart) / tot_clockFreq.QuadPart;
		
	printf("Total processing Time_GPU : %f ms\n", total_Time_GPU * 1000);

	Release();

	printf("\n");
	system("pause");

	FwriteGPU();

	// CPU 연산을 위한 측정 시간 초기화
	total_Time_CPU = 0;
	QueryPerformanceCounter(&tot_beginClock); // 시간측정 시작
	// CPU 연산
	CpuCal();
	QueryPerformanceCounter(&tot_endClock);
	total_Time_CPU = (double)(tot_endClock.QuadPart - tot_beginClock.QuadPart) / tot_clockFreq.QuadPart;
	printf("Total processing Time_CPU : %f ms\n", total_Time_CPU * 1000);

	printf("Time_CPU/Time_GPU = %.3lf\n", (double)total_Time_CPU / total_Time_GPU);

	system("pause");

	FwriteCPU();



	free(rgb);

	free(pix);
	free(pix_output);

	free(pixE);
	free(pixE_output);

	fclose(fp);

	return 0;
}
// kernel을 읽어서 char pointer생성
char* readSource(char* kernelPath) {

	cl_int status;
	FILE *fp;
	char *source;
	long int size;

	printf("Program file is: %s\n", kernelPath);

	fp = fopen(kernelPath, "rb");
	if (!fp) {
		printf("Could not open kernel file\n");
		exit(-1);
	}
	status = fseek(fp, 0, SEEK_END);
	if (status != 0) {
		printf("Error seeking to end of file\n");
		exit(-1);
	}
	size = ftell(fp);
	if (size < 0) {
		printf("Error getting file position\n");
		exit(-1);
	}

	rewind(fp);

	source = (char *)malloc(size + 1);

	int i;
	for (i = 0; i < size + 1; i++) {
		source[i] = '\0';
	}

	if (source == NULL) {
		printf("Error allocating space for the kernel source\n");
		exit(-1);
	}

	fread(source, 1, size, fp);
	source[size] = '\0';

	return source;
}
//디바이스 init, 커널 생성
void CLInit()
{
	int i, j;
	char * value;
	size_t valueSize;
	cl_uint platformCount;
	cl_platform_id * platforms;
	cl_uint deviceCount;
	cl_device_id * devices;
	cl_uint maxComputeUnits;

	// get all platforms
	clGetPlatformIDs(0, NULL, &platformCount);
	platforms = (cl_platform_id *)malloc(sizeof(cl_platform_id) * platformCount);
	clGetPlatformIDs(platformCount, platforms, NULL);

	for (i = 0; i < platformCount; i++) {

		// get all devices
		clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &deviceCount);
		devices = (cl_device_id *)malloc(sizeof(cl_device_id) * deviceCount);
		clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, deviceCount, devices, NULL);

		// for each device print critical attributes
		for (j = 0; j < deviceCount; j++) {
			// print device name
			clGetDeviceInfo(devices[j], CL_DEVICE_NAME, 0, NULL, &valueSize);

			value = (char *)malloc(valueSize);
			clGetDeviceInfo(devices[j], CL_DEVICE_NAME, valueSize, value, NULL);
			printf("platform %d. Device %d: %s\n", i + 1, j + 1, value);
			free(value);

			// print hardware device version
			clGetDeviceInfo(devices[j], CL_DEVICE_VERSION, 0, NULL, &valueSize);
			value = (char *)malloc(valueSize);
			clGetDeviceInfo(devices[j], CL_DEVICE_VERSION, valueSize, value, NULL);
			printf(" %d.%d Hardware version: %s\n", i + 1, 1, value);
			free(value);

			// print software driver version
			clGetDeviceInfo(devices[j], CL_DRIVER_VERSION, 0, NULL, &valueSize);
			value = (char *)malloc(valueSize);
			clGetDeviceInfo(devices[j], CL_DRIVER_VERSION, valueSize, value, NULL);
			printf(" %d.%d Software version: %s\n", i + 1, 2, value);
			free(value);

			// print c version supported by compiler for device
			clGetDeviceInfo(devices[j], CL_DEVICE_OPENCL_C_VERSION, 0, NULL, &valueSize);
			value = (char*)malloc(valueSize);
			clGetDeviceInfo(devices[j], CL_DEVICE_OPENCL_C_VERSION, valueSize, value, NULL);
			printf(" %d.%d OpenCL C version: %s\n", i + 1, 3, value);
			free(value);

			// print parallel compute units
			clGetDeviceInfo(devices[j], CL_DEVICE_MAX_COMPUTE_UNITS,
				sizeof(maxComputeUnits), &maxComputeUnits, NULL);
			printf(" %d.%d Parallel compute units: %d\n", i + 1, 4, maxComputeUnits);
		}
	}
	int platformNum;
	int deviceNum;
	printf("\n\nSELECT PLATFORM('1' ~ '%d') : ", platformCount);
	scanf("%d", &platformNum);
	printf("\n");
	printf("SELECT DEVICE('1' ~ '%d') : ", deviceCount);
	scanf("%d", &deviceNum);
	printf("\n");
	clGetDeviceIDs(platforms[platformNum - 1], CL_DEVICE_TYPE_ALL, deviceCount, devices, NULL);

	device = devices[deviceNum - 1];

	//create context
	context = clCreateContext(NULL, 1, &device, NULL, NULL, NULL);

	//create command queue
	queue = clCreateCommandQueue(context, device, 0, NULL);

	// 텍스트파일로부터 프로그램 읽기
	char * source = readSource("dithering.cl");

	// compile program
	program = clCreateProgramWithSource(context, 1,
		(const char **)&source, NULL, NULL);
	cl_int build_status;
	build_status = clBuildProgram(program, 1, &device, NULL, NULL,
		NULL);

	//커널 포인터 생성
	simpleKernel = clCreateKernel(program, "simpleKernel", NULL);

}
//버퍼생성 및 write
void bufferWrite()
{

	// 메모리 버퍼 생성
	d_pix = clCreateBuffer(context, CL_MEM_READ_WRITE,
		bpl * bph * sizeof(unsigned char), NULL, NULL);
	d_pixE = clCreateBuffer(context, CL_MEM_READ_WRITE,
		bpl * bph * sizeof(int), NULL, NULL);


	clEnqueueWriteBuffer(queue, d_pix, CL_TRUE, 0, bpl * bph * sizeof(unsigned char),
		pix_output, 0, NULL, NULL);
	clEnqueueWriteBuffer(queue, d_pixE, CL_TRUE, 0, bpl * bph * sizeof(int),
		pixE, 0, NULL, NULL);

}

void runKernel()
{
	int totalWorkItemsX = bpl;
	int totalWorkItemsY = bph;

	size_t globalSize[2] = { totalWorkItemsX, totalWorkItemsY };
	//float *minVal, *maxVal;


	// 커널 매개변수 설정 
	clSetKernelArg(simpleKernel, 0, sizeof(cl_mem), &d_pix);
	clSetKernelArg(simpleKernel, 1, sizeof(cl_mem), &d_pixE);
	clSetKernelArg(simpleKernel, 2, sizeof(int), &bpl);
	clSetKernelArg(simpleKernel, 3, sizeof(int), &div_bpl);
	clSetKernelArg(simpleKernel, 4, sizeof(int), &div_bph);

	clEnqueueNDRangeKernel(queue, simpleKernel, 2, NULL, globalSize,
		NULL, 0, NULL, NULL);
	// 완료 대기 
	clFinish(queue);

	clEnqueueReadBuffer(queue, d_pix, CL_TRUE, 0,
	bpl * bph * sizeof(unsigned char), pix_output, 0, NULL, NULL);
	/*clEnqueueReadBuffer(queue, d_pixE, CL_TRUE, 0,
		bpl * bph * sizeof(int), pixE_output, 0, NULL, NULL);*/

	/*
	for (int y = 0; y < bph; y++)
	{
		for (int x = 0; x < bpl; x++)
		{
			pix_output[y * bpl + x] = pixE_output[y * bpl + x] / 128 * 255;
		}
	}
	*/
}

void Release()
{
	// 릴리즈
	clReleaseProgram(program);
	clReleaseCommandQueue(queue);
	clReleaseContext(context);
}
void CpuCal() 
{
	int quant_err = 0;
	int tmppixel = 0;

	for (int y = 1; y < (bph - 1); y++)
	{
		for (int x = 1; x < (bpl - 1); x++)
		{
			//tmppixel = pix[y * bpl + x];
			pixE[y * bpl + x] += pix[y * bpl + x];
			pix[y * bpl + x] = pixE[y * bpl + x] / 128 * 255;

			quant_error = pixE[y * bpl + x] - pix[y * bpl + x];

			pixE[y * bpl + x + 1] += quant_error * 7 / 16;
			pixE[(y + 1) * bpl + x - 1] += quant_error * 3 / 16;
			pixE[(y + 1) * bpl + x] += quant_error * 5 / 16;
			pixE[(y + 1) * bpl + x + 1] += quant_error * 1 / 16;
				
		}
	}
	/*
	for (int i = 0; i < bpl * bph; i++)
	{
		pix[i] = pixE[i] / 128 * 255;
	}
	*/
}
void FwriteCPU()
{
	// 데이터 픽셀값을 bmp파일로 쓴다.
	FILE * fp2 = fopen("new_EDIMAGE_CPU.bmp", "wb");
	fwrite(&bfh, sizeof(bfh), 1, fp2);
	fwrite(&bih, sizeof(bih), 1, fp2);
	fwrite(rgb, sizeof(RGBQUAD), 256, fp2);

	fwrite(pix, sizeof(unsigned char), bpl * bph, fp2);
	//fwrite(pixE, sizeof(int), bpl * bph, fp2);
	fclose(fp2);
}
void FwriteGPU()
{
	// 데이터 픽셀값을 bmp파일로 쓴다.
	FILE * fp2 = fopen("new_EDIMAGE_GPU.bmp", "wb");
	fwrite(&bfh, sizeof(bfh), 1, fp2);
	fwrite(&bih, sizeof(bih), 1, fp2);
	fwrite(rgb, sizeof(RGBQUAD), 256, fp2);

	fwrite(pix_output, sizeof(unsigned char), bpl * bph, fp2);
	//fwrite(pixE_output, sizeof(int), bpl * bph, fp2);
	fclose(fp2);
}