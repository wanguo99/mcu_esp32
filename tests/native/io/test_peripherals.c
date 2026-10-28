#include "fan.h"
#include "mpu6050.h"
#include "servo.h"
#include "relay_uart.h"
#include "relay_uart_codec.h"
#include "fake_hardware.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static fan_config_t fan_config(void)
{ return (fan_config_t){.pwm_gpio=1,.tach_gpio=2,.timer=1,.channel=1,.pulses_per_revolution=2}; }
static relay_uart_config_t relay_config(void)
{
    return (relay_uart_config_t){.uart_port=1,.tx_gpio=3,.rx_gpio=4,.rts_gpio=-1,
        .baud_rate=9600,.parity=UART_PARITY_EVEN,.stop_bits=UART_STOP_BITS_1,
        .protocol=RELAY_UART_MODBUS_RTU,.channel_count=2,.address=1};
}

static void fan_speed(void)
{
    fan_handle_t fan; fan_config_t cfg=fan_config(); cfg.pwm_inverted=true;
    assert(fan_create(&cfg,&fan)==ESP_OK && test_duty==0 && test_inverted);
    fan_status_t status;
    assert(fan_get_status(fan,&status)==ESP_OK && !status.rpm_valid);
    assert(fan_set_duty(fan,101)==ESP_ERR_INVALID_ARG && test_duty==0);
    assert(fan_set_duty(fan,50)==ESP_OK && test_duty==512);
    assert(fan_set_duty(fan,100)==ESP_OK && test_duty==1024);
    test_counter_count=100; test_advance_time(1000,true);
    assert(fan_get_status(fan,&status)==ESP_OK && status.rpm_valid && fabsf(status.rpm-3000)<0.1f);
    assert(fan_stop(fan)==ESP_OK && test_duty==0);
    test_counter_count=120; test_advance_time(1000,true);
    fan_get_status(fan,&status); assert(status.duty_percent==0 && fabsf(status.rpm-600)<0.1f);
    test_advance_time(1000,true); fan_get_status(fan,&status); assert(status.rpm_valid && status.rpm==0);
    test_advance_time(4000,false); fan_get_status(fan,&status); assert(!status.rpm_valid);
    assert(fan_destroy(fan)==ESP_OK);
}

static void fan_validation(void)
{
    fan_handle_t fan; fan_config_t cfg=fan_config();
    cfg.pulses_per_revolution=0; assert(fan_create(&cfg,&fan)==ESP_ERR_INVALID_ARG && !fan);
    cfg=fan_config(); cfg.tach_gpio=cfg.pwm_gpio; assert(fan_create(&cfg,&fan)==ESP_ERR_INVALID_ARG);
    cfg=fan_config(); cfg.timer=0; assert(fan_create(&cfg,&fan)==ESP_ERR_INVALID_ARG); // Camera XCLK.
    const int reserved[]={7,10,19,20,26,33,35,43,44,48};
    for (unsigned i=0;i<sizeof(reserved)/sizeof(*reserved);++i) {
        cfg=fan_config(); cfg.pwm_gpio=reserved[i]; assert(fan_create(&cfg,&fan)==ESP_ERR_INVALID_ARG);
    }
    cfg=fan_config(); cfg.tach_gpio=-1; cfg.pulses_per_revolution=0;
    assert(fan_create(&cfg,&fan)==ESP_OK);
    fan_status_t status; fan_get_status(fan,&status); assert(!status.tach_available && !status.rpm_valid);
    assert(fan_destroy(fan)==ESP_OK);
}

static void fan_cleanup(void)
{
    fan_handle_t fan; fan_config_t cfg=fan_config();
    test_fail_counter_channel=1; assert(fan_create(&cfg,&fan)==ESP_FAIL && !fan);
    test_fail_counter_channel=0; assert(fan_create(&cfg,&fan)==ESP_OK);
    assert(fan_destroy(fan)==ESP_OK); assert(fan_create(&cfg,&fan)==ESP_OK);
    assert(fan_destroy(fan)==ESP_OK);
}

static void resource_conflicts(void)
{
    servo_handle_t servo;
    servo_config_t sc={.gpio=3,.timer=1,.channel=1,.frequency_hz=50,
        .min_pulse_us=500,.max_pulse_us=2500,.min_angle=0,.max_angle=180};
    assert(servo_create(&sc,&servo)==ESP_OK);
    fan_config_t fc=fan_config(); fc.channel=2; fan_handle_t fan;
    assert(fan_create(&fc,&fan)==ESP_ERR_INVALID_STATE); // Different frequency/resolution on timer.
    fc.timer=2; fc.channel=1; assert(fan_create(&fc,&fan)==ESP_ERR_INVALID_STATE); // Same channel.
    fc.channel=2; assert(fan_create(&fc,&fan)==ESP_OK);
    relay_uart_config_t rc=relay_config(); relay_uart_handle_t relay;
    assert(relay_uart_create(&rc,&relay)==ESP_ERR_INVALID_STATE); // Servo GPIO.
    rc.tx_gpio=2; assert(relay_uart_create(&rc,&relay)==ESP_ERR_INVALID_STATE); // Tach GPIO.
    assert(fan_destroy(fan)==ESP_OK); assert(servo_destroy(servo)==ESP_OK);
    rc=relay_config(); assert(relay_uart_create(&rc,&relay)==ESP_OK);
    assert(relay_uart_destroy(relay)==ESP_OK);
}

static void modbus_vectors(void)
{
    uint8_t request[8]; bool on=false;
    const uint8_t expected[]={1,5,0,0,0xff,0,0x8c,0x3a};
    relay_modbus_request(1,0,false,true,request); assert(!memcmp(request,expected,8));
    assert(relay_modbus_response(request,expected,8,&on)==ESP_OK && on);
    uint8_t bad[8]; memcpy(bad,expected,8); bad[7]^=1;
    assert(relay_modbus_response(request,bad,8,&on)==ESP_ERR_INVALID_CRC);
    assert(relay_modbus_response(request,expected,4,&on)==ESP_ERR_INVALID_ARG);
}

static void relay_modbus(void)
{
    relay_uart_config_t cfg=relay_config(); cfg.first_coil=10; relay_uart_handle_t relay;
    assert(relay_uart_create(&cfg,&relay)==ESP_OK && test_uart_writes==0);
    assert(relay_uart_set(relay,1,true)==ESP_OK);
    assert(test_uart_last[3]==11 && test_uart_last[4]==0xff && test_uart_reads==8);
    relay_uart_command_status_t status; relay_uart_get_command_status(relay,1,&status);
    assert(status.command_sent && status.requested_on && status.acknowledged);
    bool on=false; assert(relay_uart_read(relay,0,&on)==ESP_OK && on);
    assert(test_uart_last[1]==1 && test_uart_last[3]==10);
    assert(relay_uart_set(relay,2,true)==ESP_ERR_INVALID_ARG);
    unsigned writes=test_uart_writes; assert(relay_uart_destroy(relay)==ESP_OK && test_uart_writes==writes);
}

static void relay_failures(void)
{
    relay_uart_config_t cfg=relay_config(); relay_uart_handle_t relay;
    assert(relay_uart_create(&cfg,&relay)==ESP_OK);
    const int errors[]={ESP_ERR_TIMEOUT,ESP_ERR_INVALID_CRC,ESP_ERR_INVALID_RESPONSE,ESP_ERR_INVALID_RESPONSE};
    for(int mode=1;mode<=4;++mode) {
        test_uart_reply_mode=mode;
        assert(relay_uart_set(relay,0,true)==errors[mode-1]);
        relay_uart_command_status_t status; relay_uart_get_command_status(relay,0,&status);
        assert(status.command_sent && !status.acknowledged);
        bool on=false; assert(relay_uart_read(relay,0,&on)==errors[mode-1] && !on);
    }
    test_uart_reply_mode=0; assert(relay_uart_set(relay,0,false)==ESP_OK);
    assert(relay_uart_destroy(relay)==ESP_OK);
}

static void relay_fixed(void)
{
    relay_uart_channel_frames_t frames={.on={.data={0xa0,1,1,0xa2},.length=4},
        .off={.data={0xa0,1,0,0xa1},.length=4}};
    relay_uart_config_t cfg=relay_config(); cfg.protocol=RELAY_UART_FIXED_FRAMES; cfg.channel_count=1;
    cfg.frames=&frames; cfg.rx_gpio=-1;
    test_fixed_reply(NULL,0); relay_uart_handle_t relay;
    assert(relay_uart_create(&cfg,&relay)==ESP_OK && test_uart_writes==0);
    frames.on.data[0]=0; // Driver must own its copy.
    assert(relay_uart_set(relay,0,true)==ESP_OK && test_uart_last[0]==0xa0 && test_uart_reads==0);
    relay_uart_command_status_t status; relay_uart_get_command_status(relay,0,&status);
    assert(status.command_sent && !status.acknowledged);
    bool on=false; assert(relay_uart_read(relay,0,&on)==ESP_ERR_NOT_SUPPORTED);
    assert(relay_uart_destroy(relay)==ESP_OK);
    frames.on.data[0]=0xa0; frames.ack_on=(relay_uart_frame_t){.data={0x55},.length=1};
    cfg.rx_gpio=4; assert(relay_uart_create(&cfg,&relay)==ESP_OK);
    uint8_t ack=0x56; test_fixed_reply(&ack,1);
    assert(relay_uart_set(relay,0,true)==ESP_ERR_INVALID_RESPONSE);
    ack=0x55; test_fixed_reply(&ack,1); assert(relay_uart_set(relay,0,true)==ESP_OK);
    relay_uart_get_command_status(relay,0,&status); assert(status.acknowledged);
    assert(relay_uart_destroy(relay)==ESP_OK);
}

static void relay_validation(void)
{
    relay_uart_config_t cfg=relay_config(); relay_uart_handle_t relay, other;
    cfg.uart_port=0; assert(relay_uart_create(&cfg,&relay)==ESP_ERR_INVALID_ARG && !relay);
    cfg=relay_config(); cfg.address=0; assert(relay_uart_create(&cfg,&relay)==ESP_ERR_INVALID_ARG);
    cfg=relay_config(); cfg.first_coil=65535; assert(relay_uart_create(&cfg,&relay)==ESP_ERR_INVALID_ARG);
    cfg=relay_config(); cfg.rx_gpio=-1; assert(relay_uart_create(&cfg,&relay)==ESP_ERR_INVALID_ARG);
    cfg=relay_config(); assert(relay_uart_create(&cfg,&relay)==ESP_OK);
    cfg.tx_gpio=5; cfg.rx_gpio=6; assert(relay_uart_create(&cfg,&other)==ESP_ERR_INVALID_STATE && !other);
    cfg.uart_port=2; assert(relay_uart_create(&cfg,&other)==ESP_OK);
    assert(relay_uart_destroy(other)==ESP_OK && relay_uart_destroy(relay)==ESP_OK);
    assert(relay_uart_create(&cfg,&other)==ESP_OK && relay_uart_destroy(other)==ESP_OK);
}

static void put_word(unsigned reg, int16_t value)
{ test_i2c_registers[reg]=(uint16_t)value >> 8; test_i2c_registers[reg+1]=(uint16_t)value & 0xff; }

static void mpu_initialization(void)
{
    test_i2c_reset(); mpu6050_config_t cfg=MPU6050_CONFIG_DEFAULT(1,2); cfg.address=0x69;
    mpu6050_handle_t sensor; assert(mpu6050_create(&cfg,&sensor)==ESP_OK);
    assert(test_i2c_address==0x69 && test_i2c_buses==1 && test_i2c_devices==1);
    assert(test_i2c_registers[0x6b]==1 && test_i2c_registers[0x6c]==0);
    assert(test_i2c_registers[0x19]==9 && test_i2c_registers[0x1a]==3);
    assert(test_i2c_registers[0x1b]==0 && test_i2c_registers[0x1c]==0);
    assert(mpu6050_destroy(sensor)==ESP_OK && !test_i2c_buses && !test_i2c_devices);
}

static void mpu_scales(void)
{
    const float accel_lsb[]={16384,8192,4096,2048}, gyro_lsb[]={131,65.5f,32.8f,16.4f};
    const int16_t accel[]={16384,-8192,-32768}, gyro[]={131,-655,32767};
    for(unsigned a=0;a<4;++a) for(unsigned g=0;g<4;++g) {
        test_i2c_reset(); mpu6050_config_t cfg=MPU6050_CONFIG_DEFAULT(1,2);
        cfg.accel_range=(mpu6050_accel_range_t)a; cfg.gyro_range=(mpu6050_gyro_range_t)g;
        mpu6050_handle_t sensor; assert(mpu6050_create(&cfg,&sensor)==ESP_OK);
        assert(test_i2c_registers[0x1c]==(a<<3) && test_i2c_registers[0x1b]==(g<<3));
        for(unsigned i=0;i<3;++i) { put_word(0x3b+i*2,accel[i]); put_word(0x43+i*2,gyro[i]); }
        put_word(0x41,-340); unsigned reads=test_i2c_reads; mpu6050_sample_t sample;
        assert(mpu6050_read(sensor,&sample)==ESP_OK && test_i2c_reads==reads+1);
        assert(sample.timestamp_us>0 && sample.raw_temperature==-340 && fabsf(sample.temperature_c-35.53f)<0.001f);
        for(unsigned i=0;i<3;++i) {
            assert(sample.raw_accel[i]==accel[i] && sample.raw_gyro[i]==gyro[i]);
            assert(fabsf(sample.accel_m_s2[i]-accel[i]*9.80665f/accel_lsb[a])<0.001f);
            assert(fabsf(sample.gyro_dps[i]-gyro[i]/gyro_lsb[g])<0.001f);
        }
        assert(mpu6050_destroy(sensor)==ESP_OK);
    }
}

static void mpu_identity(void)
{
    test_i2c_reset(); test_i2c_registers[0x75]=0x70;
    mpu6050_config_t cfg=MPU6050_CONFIG_DEFAULT(1,2); mpu6050_handle_t sensor;
    assert(mpu6050_create(&cfg,&sensor)==ESP_ERR_NOT_SUPPORTED && !sensor);
    assert(!test_i2c_buses && !test_i2c_devices && !test_i2c_writes);
    test_i2c_registers[0x75]=0x68; assert(mpu6050_create(&cfg,&sensor)==ESP_OK);
    assert(mpu6050_destroy(sensor)==ESP_OK);
}

static void mpu_cleanup(void)
{
    mpu6050_config_t cfg=MPU6050_CONFIG_DEFAULT(1,2); mpu6050_handle_t sensor;
    for(int step=1;step<=10;++step) {
        test_i2c_reset(); test_i2c_fail_step=step;
        assert(mpu6050_create(&cfg,&sensor)==ESP_FAIL && !sensor);
        assert(!test_i2c_buses && !test_i2c_devices);
        test_i2c_reset(); assert(mpu6050_create(&cfg,&sensor)==ESP_OK);
        assert(mpu6050_destroy(sensor)==ESP_OK);
    }
}

static void mpu_sleep(void)
{
    test_i2c_reset(); mpu6050_config_t cfg=MPU6050_CONFIG_DEFAULT(1,2); mpu6050_handle_t sensor;
    assert(mpu6050_create(&cfg,&sensor)==ESP_OK);
    mpu6050_sample_t output; memset(&output,0xa5,sizeof(output));
    mpu6050_sample_t original=output;
    test_i2c_error=ESP_ERR_TIMEOUT;
    assert(mpu6050_set_sleep(sensor,true)==ESP_ERR_TIMEOUT);
    assert(mpu6050_read(sensor,&output)==ESP_ERR_TIMEOUT && !memcmp(&output,&original,sizeof(output)));
    test_i2c_error=0; assert(mpu6050_set_sleep(sensor,true)==ESP_OK && test_i2c_registers[0x6b]==0x41);
    unsigned reads=test_i2c_reads;
    assert(mpu6050_read(sensor,&output)==ESP_ERR_INVALID_STATE && test_i2c_reads==reads);
    assert(!memcmp(&output,&original,sizeof(output)));
    assert(mpu6050_set_sleep(sensor,false)==ESP_OK && test_i2c_registers[0x6b]==1);
    assert(mpu6050_read(sensor,&output)==ESP_OK && mpu6050_destroy(sensor)==ESP_OK);
}

static void mpu_resources(void)
{
    test_i2c_reset(); mpu6050_config_t cfg=MPU6050_CONFIG_DEFAULT(1,2); mpu6050_handle_t sensor, second;
    cfg.i2c_port=1; assert(mpu6050_create(&cfg,&sensor)==ESP_ERR_INVALID_ARG); // Camera bus, even while off.
    cfg.i2c_port=0; cfg.address=0x67; assert(mpu6050_create(&cfg,&sensor)==ESP_ERR_INVALID_ARG);
    cfg.address=0x68; cfg.sda_gpio=19; assert(mpu6050_create(&cfg,&sensor)==ESP_ERR_INVALID_ARG);
    cfg.sda_gpio=1; cfg.scl_gpio=7; assert(mpu6050_create(&cfg,&sensor)==ESP_ERR_INVALID_ARG);
    cfg.scl_gpio=2; cfg.dlpf=7; assert(mpu6050_create(&cfg,&sensor)==ESP_ERR_INVALID_ARG);
    cfg.dlpf=3; assert(mpu6050_create(&cfg,&sensor)==ESP_OK);
    cfg.sda_gpio=3; cfg.scl_gpio=4;
    assert(mpu6050_create(&cfg,&second)==ESP_ERR_INVALID_STATE && !second); // Same controller.
    fan_config_t fc=fan_config(); fan_handle_t fan;
    assert(fan_create(&fc,&fan)==ESP_ERR_INVALID_STATE); // I2C pins.
    assert(mpu6050_destroy(sensor)==ESP_OK);
    assert(fan_create(&fc,&fan)==ESP_OK && fan_destroy(fan)==ESP_OK);
}

int main(int argc, char **argv)
{
    struct { const char *name; void (*run)(void); } tests[]={
        {"fan_speed",fan_speed},{"fan_validation",fan_validation},{"fan_cleanup",fan_cleanup},
        {"resource_conflicts",resource_conflicts},{"modbus_vectors",modbus_vectors},
        {"relay_modbus",relay_modbus},{"relay_failures",relay_failures},
        {"relay_fixed",relay_fixed},{"relay_validation",relay_validation},
        {"mpu_initialization",mpu_initialization},{"mpu_scales",mpu_scales},
        {"mpu_identity",mpu_identity},{"mpu_cleanup",mpu_cleanup},
        {"mpu_sleep",mpu_sleep},{"mpu_resources",mpu_resources},
    };
    assert(argc==2);
    for(unsigned i=0;i<sizeof(tests)/sizeof(*tests);++i) if(!strcmp(argv[1],tests[i].name)) {
        tests[i].run(); puts(tests[i].name); return 0;
    }
    return 1;
}
