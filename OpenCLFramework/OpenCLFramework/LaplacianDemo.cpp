#include "LaplacianDemo.h"


void LaplacianDemo::load_parameters(const Parameters &params)
{
	try
	{
		gray_ = params.get_bool("gray");
	}
	catch (const std::invalid_argument &e)
	{
		std::cerr << "arg does not exist: " << e.what() << std::endl;
		exit(1);
	}
}

void LaplacianDemo::init_parameters(Parameters &params)
{
	params_ = &params;
}

void LaplacianDemo::compile_program(OpenCLBasic *oclobjects)
{
	oclobjects_ = oclobjects;
	// create program
	oprogram_ = new OpenCLProgramMultipleKernels(*oclobjects_, L"BasicKernels.cl", "");
	gradKernel_ = (*oprogram_)["gradient"];
	divKernel_ = (*oprogram_)["divergence"];
	l2Kernel_ = (*oprogram_)["l2_norm"];
}

void LaplacianDemo::init_program_args(const float *input, int width,
	int height, int nchannels, size_t nbytesI)
{
	w_ = width;
	h_ = height;
	nc_ = nchannels;
	h_in = input;
	cl_int result;
	//Creation and allocation of the device buffers for the kernels
	d_in = clCreateBuffer(oclobjects_->context, CL_MEM_READ_ONLY |
		CL_MEM_COPY_HOST_PTR, nbytesI, (void *)input, &result);
	if (result != CL_SUCCESS)
	{
		cout << "Error while initializing input data:" << getErrorString(result) << endl;
		exit(1);
	}
	d_gradX = clCreateBuffer(oclobjects_->context, CL_MEM_READ_WRITE, nbytesI, NULL, &result);
	if (result != CL_SUCCESS)
	{
		cout << "Error while initializing input data:" << getErrorString(result) << endl;
		exit(1);
	}
	d_gradY = clCreateBuffer(oclobjects_->context, CL_MEM_READ_WRITE, nbytesI, NULL, &result);
	if (result != CL_SUCCESS)
	{
		cout << "Error while initializing input data:" << getErrorString(result) << endl;
		exit(1);
	}
	d_divOut = clCreateBuffer(oclobjects_->context, CL_MEM_READ_WRITE,
		nbytesI, NULL, &result);
	if (result != CL_SUCCESS)
	{
		cout << "Error while initializing output data:" << getErrorString(result) << endl;
		exit(1);
	}

	numberOfValues_ = width*height;
	nbytesO_ = sizeof(float)*numberOfValues_;

	d_out = clCreateBuffer(oclobjects_->context, CL_MEM_WRITE_ONLY,
		nbytesO_, NULL, &result);
	if (result != CL_SUCCESS)
	{
		cout << "Error while initializing output data:" << getErrorString(result) << endl;
		exit(1);
	}

	//We will tell OpenCL what are the arguments for the kernel using their index the
	//declaration of the kernel (see kernel sources) : input at index 0 and output at index 1
	result = CL_SUCCESS;
	result |= clSetKernelArg(gradKernel_, 0, sizeof(cl_mem), &d_in);
	result |= clSetKernelArg(gradKernel_, 1, sizeof(cl_mem), &d_gradX);
	result |= clSetKernelArg(gradKernel_, 2, sizeof(cl_mem), &d_gradY);
	result |= clSetKernelArg(gradKernel_, 3, sizeof(int), &w_);
	result |= clSetKernelArg(gradKernel_, 4, sizeof(int), &h_);
	result |= clSetKernelArg(gradKernel_, 5, sizeof(int), &nc_);
	if (result != CL_SUCCESS)
	{
		cout << "Error while setting gradKernel_ arguments: " << getErrorString(result) << endl;
		exit(1);
	}

	result = CL_SUCCESS;
	result |= clSetKernelArg(divKernel_, 0, sizeof(cl_mem), &d_gradX);
	result |= clSetKernelArg(divKernel_, 1, sizeof(cl_mem), &d_gradY);
	result |= clSetKernelArg(divKernel_, 2, sizeof(cl_mem), &d_divOut);
	result |= clSetKernelArg(divKernel_, 3, sizeof(int), &w_);
	result |= clSetKernelArg(divKernel_, 4, sizeof(int), &h_);
	result |= clSetKernelArg(divKernel_, 5, sizeof(int), &nc_);
	if (result != CL_SUCCESS)
	{
		cout << "Error while setting divKernel_ arguments: " << getErrorString(result) << endl;
		exit(1);
	}

	result = CL_SUCCESS;
	result |= clSetKernelArg(l2Kernel_, 0, sizeof(cl_mem), &d_divOut);
	result |= clSetKernelArg(l2Kernel_, 1, sizeof(cl_mem), &d_out);
	result |= clSetKernelArg(l2Kernel_, 2, sizeof(int), &w_);
	result |= clSetKernelArg(l2Kernel_, 3, sizeof(int), &h_);
	result |= clSetKernelArg(l2Kernel_, 4, sizeof(int), &nc_);
	if (result != CL_SUCCESS)
	{
		cout << "Error while setting l2Kernel_ arguments: " << getErrorString(result) << endl;
		exit(1);
	}
}

void LaplacianDemo::execute_program()
{
	//Declarations
	cl_uint   workDim = 2;                      //We can use dimensions to organize data.
	//identfication values to work-items
	size_t    localWorkSize[3] = { 32, 8, 0 };
	size_t    globalWorkSize[3] = { ((w_ + localWorkSize[0] - 1) / localWorkSize[0])*localWorkSize[0],
		(((h_*nc_) + localWorkSize[1] - 1) / localWorkSize[1])*localWorkSize[1], 0 }; //Number of values for each dimension we use
	cl_event  kernelExecEvent;                  //The event for the execution of the kernel

	//Execution
	cl_int result = clEnqueueNDRangeKernel(oclobjects_->queue, gradKernel_, workDim,
		NULL, globalWorkSize, localWorkSize, 0, NULL, NULL);
	if (result != CL_SUCCESS)
	{
		cerr << "Error while executing the gradient kernel: " << getErrorString(result) << endl;
		exit(1);
	}

	clFinish(oclobjects_->queue);

	result = clEnqueueNDRangeKernel(oclobjects_->queue, divKernel_, workDim,
		NULL, globalWorkSize, localWorkSize, 0, NULL, NULL);
	if (result != CL_SUCCESS)
	{
		cerr << "Error while executing the divergence kernel: " << getErrorString(result) << endl;
		exit(1);
	}

	clFinish(oclobjects_->queue);
	// work per whole pixel instead of per channel value
	globalWorkSize[1] = ((h_ + localWorkSize[1] - 1) / localWorkSize[1])*localWorkSize[1];

	result = clEnqueueNDRangeKernel(oclobjects_->queue, l2Kernel_, workDim,
		NULL, globalWorkSize, localWorkSize, 0, NULL, NULL);
	if (result != CL_SUCCESS)
	{
		cerr << "Error while executing the divergence kernel: " << getErrorString(result) << endl;
		exit(1);
	}
	//Declarations
	cl_bool     blockingRead = CL_TRUE;
	size_t offset = 0;
	cl_event    readResultsEvent;           //The event for the execution of the kernel


	//Allocations
	h_out = (float*)malloc(nbytesO_);

	//Waiting for all commands to end. Note we coul have use the kernelExecEvent as an event
	//to wait the end. But the clFinish function is simplier in this case.
	clFinish(oclobjects_->queue);

	// read outputs from device
	clEnqueueReadBuffer(oclobjects_->queue, d_out, blockingRead, offset, nbytesO_,
		h_out, 0, NULL, &readResultsEvent);
}

void LaplacianDemo::display_output()
{

	Mat mOut(h_, w_, CV_32FC1), mIn(h_, w_, GET_TYPE(gray_));
	convert_layered_to_mat(mOut, h_out);
	convert_layered_to_mat(mIn, h_in);

	showImage("Input", mIn, 100, 100);
	showImage("Laplacian Norm", mOut, 100 + w_ + 40, 100);
}

void LaplacianDemo::deinit_program_args()
{
	free(h_out);
	cl_int result = clReleaseMemObject(d_out);
	result |= clReleaseMemObject(d_divOut);
	result |= clReleaseMemObject(d_in);
	result |= clReleaseMemObject(d_gradX);
	result |= clReleaseMemObject(d_gradY);
	if (result != CL_SUCCESS)
	{
		cout << "Error while deallocating device resources: " << getErrorString(result) << endl;
		exit(1);
	}
}

void LaplacianDemo::deinit_parameters()
{
	delete oprogram_;
	//params_->clear();
}
LaplacianDemo::~LaplacianDemo()
{
	deinit_parameters();
}
