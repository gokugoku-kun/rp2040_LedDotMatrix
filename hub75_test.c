//
// Copyright (c) 2025 gokugoku.
//
// SPDX-License-Identifier: BSD-3-Clause
//
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "hardware/interp.h"
#include "hardware/clocks.h"
#include "hub75.pio.h"
#include "hub75_image_data.c"

//ディスプレイモード
enum
{
    DISPLAYMODE_SCROLL,
    DISPLAYMODE_FLIP,
    DISPLAYMODE_MAX,
};
#define HUB75_DISPLAYMODE (DISPLAYMODE_FLIP)
static uint32_t scroll_idx =0;

//周期割り込み
static bool __isr __time_critical_func(timer_callback)( repeating_timer_t *rt );
static uint32_t time_1ms_down = 50;

//LED数定義
#define LED_MATRIX_COL          (64)
#define LED_MATRIX_ROW          (64)
#define LED_MATRIX_ADDRES_MAX   (32)

//PIOに渡すRGBデータのリングバッファ
typedef struct  
{
    uint32_t bgr888;
    //RGBデータを9段階時分割したデータ
        // MSB                               LSB
        // |unuse(8bit)|B(8bit)|G(8bit)|R(8bit)|
    uint32_t ope;
    //PIO制御用データ
        // MSB                                                             LSB
        // |unuse(29bit)|フレームエンド(1bit)|行エンド(1bit)|上下セクション(1bit)|
} hub75_pio_data;

typedef struct  
{
    hub75_pio_data data[LED_MATRIX_ADDRES_MAX*LED_MATRIX_COL][2];
    //↑PIOにDMAで上n行目→下n行目→上n+1行目…の順で送れるようなメモリ配置に調整した定義
} hub75_buff;

enum eHUB75_BUF_IDX
{
    HUB75_BUF_0,
    HUB75_BUF_1,
    HUB75_BUF_2,
    HUB75_BUF_3,
    HUB75_BUF_MAX
};

hub75_buff hub75_buff_array[HUB75_BUF_MAX];
uint16_t hub75_data_buff_write_idx = HUB75_BUF_0;
uint16_t hub75_data_buff_read_idx = HUB75_BUF_0;
#define HUB75_BUF_NEXT(n) (((n) + 1) % HUB75_BUF_MAX)
#define HUB75_BIT_DEPTH         (8)
static void __time_critical_func(hub75_Make_buff)(void);

//PIOに渡すアドレスデータ
uint32_t hub75_address_array[LED_MATRIX_ADDRES_MAX] =
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,};

//GPIO
#define DATA_BASE_PIN 0
#define DATA_N_PINS 6
#define CLK_PIN 11

#define ROWSEL_BASE_PIN 6
#define ROWSEL_N_PINS 5
#define STROBE_PIN 12
#define OEN_PIN 13

#define DEBUG_PIN               (14)

//PIO
static PIO hub75_pio = pio0;
static uint data_prog_offs;
static uint row_prog_offs;
static uint sm_data = 0;
static uint sm_row = 1;

//DMA
static int dma_chan = 2;
static dma_channel_config dma_config;
static int dma_chan_ad = 3;
static dma_channel_config dma_config_ad;
static void __isr __time_critical_func(dma_handler)(void);



int main()
{
    stdio_init_all();
    sleep_ms(5000);
    printf("test01\n");
    /* GPIO初期設定 */
    {
        uint8_t i;
        for ( i=0; i< DATA_N_PINS;i++) gpio_init( DATA_BASE_PIN+i );
        for ( i=0; i< ROWSEL_N_PINS;i++) gpio_init( ROWSEL_BASE_PIN+i );
        gpio_init( CLK_PIN);
        gpio_init( STROBE_PIN );
        gpio_init( OEN_PIN);
        for ( i=0; i< DATA_N_PINS;i++) gpio_set_dir( DATA_BASE_PIN+i , GPIO_OUT );
        for ( i=0; i< ROWSEL_N_PINS;i++) gpio_set_dir( ROWSEL_BASE_PIN+i , GPIO_OUT );
        gpio_set_dir( CLK_PIN, GPIO_OUT );
        gpio_set_dir( STROBE_PIN , GPIO_OUT );
        gpio_set_dir( OEN_PIN, GPIO_OUT );
        for ( i=0; i< DATA_N_PINS;i++) gpio_put( DATA_BASE_PIN+i , 0 );
        for ( i=0; i< ROWSEL_N_PINS;i++) gpio_put( ROWSEL_BASE_PIN+i , 0 );
        gpio_put( CLK_PIN, 0 );
        gpio_put( STROBE_PIN , 0 );
        gpio_put( OEN_PIN, 0 );
        sleep_ms(5000);
        gpio_put( STROBE_PIN , 1 );
        printf("test02\n");

        gpio_init( DEBUG_PIN);
        gpio_set_dir( DEBUG_PIN , GPIO_OUT );
    }
    /* PIO初期設定*/
    {
        data_prog_offs = pio_add_program(hub75_pio, &pio_hub75_data_program);
        row_prog_offs = pio_add_program(hub75_pio, &pio_hub75_address_program);
        pio_hub75_data_program_init(hub75_pio, sm_data, data_prog_offs, DATA_BASE_PIN, DATA_N_PINS, CLK_PIN, 1);
        pio_hub75_address_program_init(hub75_pio, sm_row, row_prog_offs, ROWSEL_BASE_PIN, ROWSEL_N_PINS, STROBE_PIN,2);
    }
    /* dma初期設定(データ) */
    {
        dma_chan = dma_claim_unused_channel(true);
        dma_config = dma_channel_get_default_config(dma_chan);
        channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config, true);
        channel_config_set_dreq(&dma_config, DREQ_PIO0_TX0+sm_data);
        dma_channel_configure(
            dma_chan,
            &dma_config,
            &hub75_pio->txf[sm_data], // Write address (only need to set this once)
            NULL,             // Don't provide a read address yet
            0, // Write the same value many times, then halt and interrupt
            false             // Don't start yet
        );
        dma_channel_set_irq0_enabled(dma_chan, true);
        irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
        irq_set_enabled(DMA_IRQ_0, true);
    }
    /* dma初期設定(アドレス) */
    {
        dma_chan_ad = dma_claim_unused_channel(true);
        dma_config_ad = dma_channel_get_default_config(dma_chan_ad);
        channel_config_set_transfer_data_size(&dma_config_ad, DMA_SIZE_32);
        channel_config_set_read_increment(&dma_config_ad, true);
        channel_config_set_dreq(&dma_config_ad, DREQ_PIO0_TX0+sm_row);
        dma_channel_configure(
            dma_chan_ad,
            &dma_config_ad,
            &hub75_pio->txf[sm_row], // Write address (only need to set this once)
            NULL,             // Don't provide a read address yet
            0, // Write the same value many times, then halt and interrupt
            false             // Don't start yet
        );
    }
    /* タイマ初期設定 */
    {
        static repeating_timer_t timer;
        /* 1msインターバルタイマ設定 */
        add_repeating_timer_us( -1000, &timer_callback, NULL, &timer );
    }

    hub75_Make_buff();//先にバッファを作成しておく
    hub75_Make_buff();
    dma_handler();//最初だけ手動実行

    printf("test03\n");

    /* メインループ */
    while ( 1 )
    {
        hub75_Make_buff();
    }

    return 0;
}

static void __time_critical_func(hub75_Make_buff)(void)
{
        /* バッファ作成 */
        if ( HUB75_BUF_NEXT(hub75_data_buff_write_idx) != hub75_data_buff_read_idx )
        {
gpio_put( DEBUG_PIN , 0 );
            hub75_data_buff_write_idx = HUB75_BUF_NEXT(hub75_data_buff_write_idx);
            {
                for (uint8_t r = 0; r < LED_MATRIX_ROW; r++ )
                {
                    for (uint8_t c = 0; c < LED_MATRIX_COL; c++ )
                    {
                        if ( r/LED_MATRIX_ADDRES_MAX == 1 )
                        {
                            uint32_t val = 0;
                            val |= 0x01;
                            if (c == (LED_MATRIX_COL-1))
                            {
                                val |= 0x02;
                                if ( r == (LED_MATRIX_ROW-1) ) val |= 0x04;
                            }
                            hub75_buff_array[hub75_data_buff_write_idx].data[(r%LED_MATRIX_ADDRES_MAX)*LED_MATRIX_COL+c][1].bgr888=image_data[((scroll_idx+r)/LED_MATRIX_ROW)%IMAGE_DATA_SIZE][(scroll_idx+r)%LED_MATRIX_ROW][c];
                            hub75_buff_array[hub75_data_buff_write_idx].data[(r%LED_MATRIX_ADDRES_MAX)*LED_MATRIX_COL+c][1].ope=val;
                        }
                        else
                        {
                            hub75_buff_array[hub75_data_buff_write_idx].data[(r%LED_MATRIX_ADDRES_MAX)*LED_MATRIX_COL+c][0].bgr888=image_data[((scroll_idx+r)/LED_MATRIX_ROW)%IMAGE_DATA_SIZE][(scroll_idx+r)%LED_MATRIX_ROW][c];
                            hub75_buff_array[hub75_data_buff_write_idx].data[(r%LED_MATRIX_ADDRES_MAX)*LED_MATRIX_COL+c][0].ope=0;
                        }

                    }
                }
            }
gpio_put( DEBUG_PIN , 1 );
        }
        //TODO opeは毎回作成しなくてもいい
}

/* 1ms割り込みでコールすること */
static bool __isr __time_critical_func(timer_callback)( repeating_timer_t *rt )
{
    time_1ms_down--;
    if ( time_1ms_down == 0 )
    {
        switch (HUB75_DISPLAYMODE)
        {
            case DISPLAYMODE_SCROLL:
                time_1ms_down = 50;
                if ( ++scroll_idx > IMAGE_DATA_SIZE*LED_MATRIX_ROW ) { scroll_idx = 0; }
                break;
            case DISPLAYMODE_FLIP:
                time_1ms_down = 100;
                if ( scroll_idx+LED_MATRIX_ROW < IMAGE_DATA_SIZE*LED_MATRIX_ROW) { scroll_idx += LED_MATRIX_ROW; }
                else { scroll_idx = 0; }
                break;
            default:
                break;
        }

    }
    return ( true );
}

static void __isr __time_critical_func(dma_handler)(void)
{
    //TODO idx更新：フレーム作成処理が間に合わない場合は切り替えたいタイミングで作成&切替へ変更するとよい
    if ( HUB75_BUF_NEXT(hub75_data_buff_read_idx) != hub75_data_buff_write_idx ) { hub75_data_buff_read_idx = HUB75_BUF_NEXT(hub75_data_buff_read_idx); }

    dma_channel_transfer_from_buffer_now( dma_chan, &hub75_buff_array[hub75_data_buff_read_idx].data, LED_MATRIX_ADDRES_MAX*LED_MATRIX_COL*2*2 );
    dma_channel_transfer_from_buffer_now( dma_chan_ad, &hub75_address_array, LED_MATRIX_ADDRES_MAX );
    dma_hw->ints0 = 1u << dma_chan;
}
