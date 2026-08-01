## Frames and Frame Sizing, B200/B210 

- The maximum hardware ring buffer allocation (`num_recv_frames`) on the Ettus B210 stock firmware _defaults to 32 frames_ 
  but can be manually increased in software up to 512 or 1000+ frames depending on host system allocation. 

- The internal FPGA block RAM (BRAM) FIFOs on the Spartan-6 XC6SLX150 are fixed by the bitstream architecture, while USB 
  DMA transfers scale dynamically up to host limits.

### Buffer Architecture and Limits

- **Default Host Frames:** 32 receive frames (`num_recv_frames`)

- **Recommended Adjustment:** Up to 512–1000 frames for high-throughput streaming without overruns

- **Internal FPGA BRAM:** Fixed hardware FIFO allocation inside the Spartan-6
	
	- **Bus Limitation:** USB 3.0 SuperSpeed interface caps sustained sample rates at 61.44 MS/s quadrature per channel configuration.

For the Ettus USRP B210 running on stock UHD firmware over USB 3.0: 

- **Default size** of a single receive frame (`recv_frame_size`) is **8,176 bytes**.  

### Frame Size Breakdown by Connection Type

- **USB 3.0 (SuperSpeed): 8,176 bytes (default).** It is intentionally set just under 8,192 bytes (and not a multiple of 512) 
   to optimize USB controller bulk transfer mechanics.

 	- USB 2.0 (High-Speed): 1,024 bytes (default fallback).  

- **Maximum Driver Cap: 16,360 bytes** (`B200_USB_DATA_MAX_RECV_FRAME_SIZE`). 

 ### What This Means for Sample Payload

 - In UHD, data is packed into these frames alongside a small transport layer header. 
 - If you are using the standard sc16 (Complex Short) wire format:
 	- Each sample consumes 4 bytes (2 bytes for In-phase, 2 bytes for Quadrature).
 	- A standard 8,176-byte frame carries approximately 2,040 samples per packet after subtracting transport metadata overhead.


### Total Allocation Calculation

- When utilizing the default configuration of 32 receive frames, the host system allocates a relatively small initial ring buffer:

	- `32 frames * 8,176 bytes/frame ~ 261.6 KB` of total RAM pool 


**Note:** This thin margin is why memory pool starvation and "O" (overflow) indicators occur rapidly if the host application 
  fails to pull data from the thread fast enough.