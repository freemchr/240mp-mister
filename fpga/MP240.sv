//============================================================================
//
//  240-MP: MiSTer Edition — core shell ("240MP")
//
//  Derived from Menu_MiSTer, Copyright (C) 2017-2020 Sorgelig
//  (GPL-2.0-or-later) and Template_MiSTer (GPL-2.0-or-later).
//  Modifications Copyright (C) 2026 the 240mp-mister project.
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//  more details.
//
//============================================================================
//
//  The fabric's job here is deliberately minimal: provide 15 kHz-family video
//  timing with a VHS-static backdrop and host the MiSTer framework (ascal
//  scaler with the HPS-writable Linux framebuffer, ALSA HPS->FPGA audio,
//  analog/composite output).  The 240-MP media app runs on the HPS ARM side,
//  draws its UI/video into /dev/fb0 (displayed by ascal over this core's
//  video), and plays audio through /dev/MrAudio.  When the framebuffer is
//  disabled you see authentic VHS snow — which is exactly the aesthetic.
//
//============================================================================

module emu
(
	`include "sys/emu_ports.vh"
);

///////// Default values for ports not used in this core /////////

assign ADC_BUS  = 'Z;
assign USER_OUT = '1;
assign {UART_RTS, UART_TXD, UART_DTR} = 0;
assign {SD_SCK, SD_MOSI, SD_CS} = 'Z;
assign {SDRAM_DQ, SDRAM_A, SDRAM_BA, SDRAM_CLK, SDRAM_CKE, SDRAM_DQML, SDRAM_DQMH, SDRAM_nWE, SDRAM_nCAS, SDRAM_nRAS, SDRAM_nCS} = 'Z;
assign {DDRAM_CLK, DDRAM_BURSTCNT, DDRAM_ADDR, DDRAM_DIN, DDRAM_BE, DDRAM_RD, DDRAM_WE} = '0;

assign VGA_SL = 0;
assign VGA_F1 = 0;
assign VGA_SCALER  = 0;
assign VGA_DISABLE = 0;
assign HDMI_FREEZE = 0;
assign HDMI_BLACKOUT = 0;
assign HDMI_BOB_DEINT = 0;

// Media audio comes from the HPS through sys/alsa.sv; the core itself is silent.
assign AUDIO_S = 1;
assign AUDIO_L = 0;
assign AUDIO_R = 0;
assign AUDIO_MIX = 0;

assign LED_DISK = 0;
assign LED_POWER = 0;
assign BUTTONS = 0;

// Free aspect (the HPS framebuffer defines the picture)
assign VIDEO_ARX = 0;
assign VIDEO_ARY = 0;

/////////////////////  FABRIC-OWNED FRAMEBUFFER  ///////////////////
// The core itself scans out a fixed DDR3 framebuffer (MISTER_FB / ascal),
// independent of Main_MiSTer's fb-terminal state.  The HPS app writes
// pixels directly via /dev/mem.
//
// Base address 0x22000000 (Main's canonical HPS framebuffer address =
// 0x20000000 + 32MB).  An on-device readback test proved the scaler's DDR
// working region corrupts the top ~40% of any buffer placed at 0x20000000
// AND 0x21000000 (they alias/overlap ascal's scratch identically), while
// 0x22000000 stays intact — it's the region the framework reserves for the
// HPS framebuffer and never overwrites unless the fb-terminal is active
// (which this core never triggers).
// Format 5'b10110 = 32bpp, BGR bit set -> app writes little-endian
// XRGB8888 words (0x00RRGGBB), same as the Phase 0 validated format.
assign FB_EN          = 1;
assign FB_FORMAT      = 5'b10110;
assign FB_WIDTH       = 12'd640;
assign FB_HEIGHT      = 12'd480;
assign FB_BASE        = 32'h22000000;
assign FB_STRIDE      = 14'd2560;
assign FB_FORCE_BLANK = 0;

assign CE_PIXEL = ce_pix;

// Breathing activity LED (as in the menu core)
reg [26:0] act_cnt;
always @(posedge clk_sys) act_cnt <= act_cnt + 1'd1;
assign LED_USER = act_cnt[26] ? act_cnt[25:18] > act_cnt[7:0] : act_cnt[25:18] <= act_cnt[7:0];

`include "build_id.v"
localparam CONF_STR = {
	"240MP;;",
	"-;",
	"O[2],TV Mode,NTSC,PAL;",
	"-;",
	"V,v",`BUILD_DATE
};

wire forced_scandoubler;
wire [127:0] status;

hps_io #(.CONF_STR(CONF_STR)) hps_io
(
	.clk_sys(clk_sys),
	.HPS_BUS(HPS_BUS),
	.forced_scandoubler(forced_scandoubler),
	.status(status)
);

////////////////////   CLOCKS   ///////////////////

wire clk_sys;   // 100 MHz
pll pll
(
	.refclk(CLK_50M),
	.rst(0),
	.outclk_0(clk_sys),
	.outclk_1(CLK_VIDEO)   // 20 MHz (10 MHz pixel rate, 15 kHz-family timing)
);

/////////////////////   VIDEO — timing only, black core video   ///////////////////
// 15 kHz NTSC/PAL timing from Menu_MiSTer.  The core's own RGB is held BLACK:
// the picture is the HPS framebuffer (ascal reads DDR when FB_EN=1), and the
// app owns the full screen.  A tinted/animated backdrop, if wanted, is better
// drawn by the app into the framebuffer than generated here in fabric — that
// way it never competes with the app's pixels.

wire PAL = status[2];

reg   [9:0] hc;
reg   [9:0] vc;

always @(posedge CLK_VIDEO) begin
	if(forced_scandoubler) ce_pix <= 1;
		else ce_pix <= ~ce_pix;

	if(ce_pix) begin
		if(hc == 637) begin
			hc <= 0;
			if(vc == (PAL ? (forced_scandoubler ? 623 : 311) : (forced_scandoubler ? 523 : 261)))
				vc <= 0;
			else
				vc <= vc + 1'd1;
		end else begin
			hc <= hc + 1'd1;
		end
	end
end

reg HBlank;
reg HSync;
reg VBlank;
reg VSync;

reg ce_pix;
always @(posedge CLK_VIDEO) begin
	if (hc == 529) HBlank <= 1;
		else if (hc == 0) HBlank <= 0;

	if (hc == 544) begin
		HSync <= 1;

		if(PAL) begin
			if(vc == (forced_scandoubler ? 609 : 304)) VSync <= 1;
				else if (vc == (forced_scandoubler ? 617 : 308)) VSync <= 0;

			if(vc == (forced_scandoubler ? 601 : 300)) VBlank <= 1;
				else if (vc == 0) VBlank <= 0;
		end
		else begin
			if(vc == (forced_scandoubler ? 490 : 245)) VSync <= 1;
				else if (vc == (forced_scandoubler ? 496 : 248)) VSync <= 0;

			if(vc == (forced_scandoubler ? 480 : 240)) VBlank <= 1;
				else if (vc == 0) VBlank <= 0;
		end
	end

	if (hc == 590) HSync <= 0;
end

assign VGA_DE  = ~(HBlank | VBlank);
assign VGA_HS  = HSync;
assign VGA_VS  = VSync;
assign VGA_G   = 8'd0;
assign VGA_R   = 8'd0;
assign VGA_B   = 8'd0;

endmodule
