
//============================================================
//
//  video.c - IOS video handling
//
//============================================================

// MAME headers
#include "emu.h"
#include "interface/audio.h"

// IOS headers
#include "iososd.h"

static void myosd_sound_init(int rate, int stereo);
static void myosd_sound_play(void *buff, int len);
static void myosd_sound_exit(void);

//============================================================
//  sound_init
//============================================================

void ios_osd_interface::sound_init()
{
    osd_printf_verbose("ios_osd_interface::sound_init\n");
    
    // Initialize audio info structure
    m_audio_info = osd::audio_info();
    m_audio_info.m_generation = 0;
    m_next_stream_id = 1;

    // Create a default sink node for iOS audio output
    osd::audio_info::node_info sink_node;
    sink_node.m_name = "ios_output";
    sink_node.m_display_name = "iOS Audio Output";
    sink_node.m_id = 1;
    sink_node.m_rate.m_default_rate = 48000;
    sink_node.m_rate.m_min_rate = 8000;
    sink_node.m_rate.m_max_rate = 48000;
    sink_node.m_port_names = {"Left", "Right"};
    sink_node.m_port_positions = {osd::channel_position::FL(), osd::channel_position::FR()};
    sink_node.m_sinks = 1;
    sink_node.m_sources = 0;    
    m_audio_info.m_nodes.push_back(sink_node);
    m_audio_info.m_default_sink = 1;
    m_audio_info.m_default_source = 0;    

    // if the host does not want to handle audio, do a default
    if (m_callbacks.sound_play == NULL)
    {
        m_callbacks.sound_init = myosd_sound_init;
        m_callbacks.sound_play = myosd_sound_play;
        m_callbacks.sound_exit = myosd_sound_exit;
    }

    m_sample_rate = options().sample_rate();

    if (strcmp(options().value(OPTION_SOUND), "none") == 0)
        m_sample_rate = 0;
    
    if (m_sample_rate != 0)
    {
        // set the startup volume
        // set_mastervolume(0);
        m_callbacks.sound_init(m_sample_rate, 1);
    }
}

//============================================================
//  sound_exit
//============================================================

void ios_osd_interface::sound_exit()
{
    osd_printf_verbose("ios_osd_interface::sound_exit\n");

    // Close all open streams
    m_audio_info.m_streams.clear();

    if (m_sample_rate != 0)
    {
        m_callbacks.sound_exit();
    }
}

//============================================================
//  osd::channel_position implementation
//============================================================

namespace osd {

std::string channel_position::name() const
{
    if (*this == FC()) return "FC";
    if (*this == FL()) return "FL";
    if (*this == FR()) return "FR";
    if (*this == RC()) return "RC";
    if (*this == RL()) return "RL";
    if (*this == RR()) return "RR";
    if (*this == HC()) return "HC";
    if (*this == HL()) return "HL";
    if (*this == HR()) return "HR";
    if (*this == BACKREST()) return "BACKREST";
    if (*this == LFE()) return "LFE";
    if (*this == ONREQ()) return "ONREQ";
    if (*this == UNKNOWN()) return "UNKNOWN";
    
   // For custom positions, return coordinates
    return "(" + std::to_string(m_x) + "," + std::to_string(m_y) + "," + std::to_string(m_z) + ")";
}

} // namespace osd


//============================================================
//    Apply attenuation
//============================================================

static void att_memcpy(void *dest, const int16_t *data, int bytes_to_copy, int attenuation)
{
    int level = (int)(pow(10.0, (float) attenuation / 20.0) * 128.0);
    int16_t *d = (int16_t *)dest;
    int count = bytes_to_copy/2;
    while (count>0)
    {
        *d++ = (*data++ * level) >> 7; /* / 128 */
        count--;
    }
}

//============================================================
//  NEW: sound interface implementation
//============================================================
bool ios_osd_interface::no_sound()
{
    return m_sample_rate == 0;
}

osd::audio_info ios_osd_interface::sound_get_information()
{
    return m_audio_info;
}

uint32_t ios_osd_interface::sound_stream_sink_open(uint32_t node, std::string name, uint32_t rate)
{
    osd_printf_verbose("ios_osd_interface::sound_stream_sink_open: node=%d name=%s rate=%d\n", node, name.c_str(), rate);
    
    osd::audio_info::stream_info stream;
    stream.m_id = m_next_stream_id++;
    stream.m_node = node;
    
    m_audio_info.m_streams.push_back(stream);
    m_audio_info.m_generation++;
    
    return stream.m_id;
}

uint32_t ios_osd_interface::sound_stream_source_open(uint32_t node, std::string name, uint32_t rate)
{
    osd_printf_verbose("ios_osd_interface::sound_stream_source_open: node=%d name=%s rate=%d\n", node, name.c_str(), rate);
    
    osd::audio_info::stream_info stream;
    stream.m_id = m_next_stream_id++;
    stream.m_node = node;
    
    m_audio_info.m_streams.push_back(stream);
    m_audio_info.m_generation++;
    
    return stream.m_id;
}

void ios_osd_interface::sound_stream_close(uint32_t id)
{
    osd_printf_verbose("ios_osd_interface::sound_stream_close: id=%d\n", id);
    
    auto it = std::find_if(m_audio_info.m_streams.begin(), m_audio_info.m_streams.end(),
                          [id](const osd::audio_info::stream_info& stream) { return stream.m_id == id; });
    
    if (it != m_audio_info.m_streams.end())
    {
        m_audio_info.m_streams.erase(it);
        m_audio_info.m_generation++;
    }
}

void ios_osd_interface::sound_stream_sink_update(uint32_t id, const int16_t *buffer, int samples_this_frame)
{
    osd_printf_verbose("ios_osd_interface::sound_stream_sink_update: id=%d samples=%d attenuation=%d\n", id, samples_this_frame, m_attenuation);

    static unsigned char bufferatt[882*2*2*10];

    if (m_sample_rate != 0)
    {
        const int16_t *output_buffer = buffer;
        
        // Apply attenuation if needed
        if (m_attenuation != 0)
        {
            if (samples_this_frame * 2 * 2 >= sizeof(bufferatt))
                samples_this_frame = sizeof(bufferatt) / (2 * 2);
                
            att_memcpy(bufferatt, buffer, samples_this_frame * sizeof(int16_t) * 2, m_attenuation);
            output_buffer = (int16_t *)bufferatt;
        }
        
        // Send to iOS audio system
        m_callbacks.sound_play((void*)output_buffer, samples_this_frame * sizeof(int16_t) * 2);
    }
}

void ios_osd_interface::sound_stream_source_update(uint32_t id, int16_t *buffer, int samples_this_frame)
{
    osd_printf_verbose("ios_osd_interface::sound_stream_source_update: id=%d samples=%d\n", id, samples_this_frame);
    
    // For iOS, we typically don't have audio input, so fill with silence
    memset(buffer, 0, samples_this_frame * sizeof(int16_t) * 2);
}

void ios_osd_interface::sound_stream_set_volumes(uint32_t id, const std::vector<float> &db)
{
    osd_printf_verbose("ios_osd_interface::sound_stream_set_volumes: id=%d\n", id);
    
    auto it = std::find_if(m_audio_info.m_streams.begin(), m_audio_info.m_streams.end(),
                          [id](osd::audio_info::stream_info& stream) { return stream.m_id == id; });
    
    if (it != m_audio_info.m_streams.end())
    {
        it->m_volumes = db;
        m_audio_info.m_generation++;
    }
}


//============================================================
//    osd_update_audio_stream
//============================================================

// void ios_osd_interface::update_audio_stream(const int16_t *buffer, int samples_this_frame)
// {
//     osd_printf_verbose("ios_osd_interface::update_audio_stream: samples=%d attenuation=%d\n", samples_this_frame, m_attenuation);

//     static unsigned char bufferatt[882*2*2*10];

//     if (m_sample_rate != 0 )
//     {
//         if (m_attenuation != 0)
//         {
//             if (samples_this_frame * 2 * 2 >= sizeof(bufferatt))
//                 samples_this_frame = sizeof(bufferatt) / (2 * 2);
                
//             att_memcpy(bufferatt,buffer,samples_this_frame * sizeof(int16_t) * 2, m_attenuation);
//             buffer = (int16_t *)bufferatt;
//         }
//         m_callbacks.sound_play((void*)buffer,samples_this_frame * sizeof(int16_t) * 2);
//     }
// }

// void ios_osd_interface::set_mastervolume(int attenuation)
// {
//     // clamp the attenuation to 0-32 range
//     if (attenuation > 0)
//         attenuation = 0;
//     if (attenuation < -32)
//         attenuation = -32;

//     m_attenuation = attenuation;
//     //m_attenuation_level = (int)(pow(10.0, (float)m_attenuation / 20.0) * 128.0);
// }

// bool ios_osd_interface::no_sound()
// {
//     return m_sample_rate == 0;
// }

//============================================================
//  default sound impl
//============================================================

#import <AudioToolbox/AudioQueue.h>
#import <AudioToolbox/AudioToolbox.h>

/* Audio Resources */
//minimum required buffers for iOS AudioQueue

#define AUDIO_BUFFERS 3

static int soundInit = 0;

typedef struct AQCallbackStruct {
    AudioQueueRef queue;
    UInt32 frameCount;
    AudioQueueBufferRef mBuffers[AUDIO_BUFFERS];
    AudioStreamBasicDescription mDataFormat;
} AQCallbackStruct;

AQCallbackStruct in;

static pthread_mutex_t sound_mutex = PTHREAD_MUTEX_INITIALIZER;

static int global_low_latency_sound  = 1;

int sound_close_AudioQueue(void);
int sound_open_AudioQueue(int rate, int bits, int stereo);
int sound_close_AudioUnit(void);
int sound_open_AudioUnit(int rate, int bits, int stereo);
void queue(unsigned char *p,unsigned size);
unsigned short dequeue(unsigned char *p,unsigned size);
inline int emptyQueue(void);

static void myosd_sound_init(int rate, int stereo)
{
    if (soundInit == 0)
    {
        if(global_low_latency_sound)
        {
            osd_printf_debug("myosd_openSound LOW LATENCY rate:%d stereo:%d \n",rate,stereo);
            sound_open_AudioUnit(rate, 16, stereo);
        }
        else
        {
            osd_printf_debug("myosd_openSound NORMAL rate:%d stereo:%d \n",rate,stereo);
            sound_open_AudioQueue(rate, 16, stereo);
        }
       
        soundInit = 1;
    }
}

static void myosd_sound_exit(void)
{
    if( soundInit == 1 )
    {
        osd_printf_debug("myosd_closeSound\n");

        if(global_low_latency_sound)
           sound_close_AudioUnit();
        else
           sound_close_AudioQueue();

        soundInit = 0;
    }
}

static void myosd_sound_play(void *buff, int len)
{
    queue((unsigned char *)buff,len);
}

//SQ buffers for sound between MAME and iOS AudioQueue. AudioQueue
//SQ callback reads from these.
//SQ Size: (48000/30fps) * bytesize * stereo * (3 buffers)
#define TAM (1600 * 2 * 2 * 3)
unsigned char ptr_buf[TAM];
unsigned head = 0;
unsigned tail = 0;

inline int fullQueue(unsigned short size){

    if(head < tail)
    {
        return head + size >= tail;
    }
    else if(head > tail)
    {
        return (head + size) >= TAM ? (head + size)- TAM >= tail : false;
    }
    else return false;
}

inline int emptyQueue(){
    return head == tail;
}

void queue(unsigned char *p,unsigned size) {
    
        if (fullQueue(size))
        {
            osd_printf_verbose("queue: FULL\n");
        }
    
        unsigned newhead;
        if(head + size < TAM)
        {
            memcpy(ptr_buf+head,p,size);
            newhead = head + size;
        }
        else
        {
            memcpy(ptr_buf+head,p, TAM -head);
            memcpy(ptr_buf,p + (TAM-head), size - (TAM-head));
            newhead = (head + size) - TAM;
        }
        pthread_mutex_lock(&sound_mutex);

        head = newhead;

        pthread_mutex_unlock(&sound_mutex);
}

unsigned short dequeue(unsigned char *p,unsigned size){

        unsigned real;
        unsigned datasize;

        if(emptyQueue())
        {
            osd_printf_verbose("dequeue: EMPTY(%d)\n", size);
            memset(p,0,size);//TODO ver si quito para que no petardee
            return 0;
        }

        pthread_mutex_lock(&sound_mutex);

        datasize = head > tail ? head - tail : (TAM - tail) + head ;
        real = datasize > size ? size : datasize;

        if(tail + real < TAM)
        {
            memcpy(p,ptr_buf+tail,real);
            tail+=real;
        }
        else
        {
            memcpy(p,ptr_buf + tail, TAM - tail);
            memcpy(p+ (TAM-tail),ptr_buf , real - (TAM-tail));
            tail = (tail + real) - TAM;
        }
    
        pthread_mutex_unlock(&sound_mutex);

        if (real < size)
        {
            osd_printf_verbose("dequeue: LOW(%d)\n", size-real);
            memset(p+real, 0, size-real);
        }
        else
        {
            osd_printf_verbose("dequeue: OK(%d)\n", size);
        }

        return real;
}

void checkStatus(OSStatus status){}


static void AQBufferCallback(void *userdata,
                             AudioQueueRef outQ,
                             AudioQueueBufferRef outQB)
{
    unsigned char *coreAudioBuffer;
    coreAudioBuffer = (unsigned char*) outQB->mAudioData;

    dequeue(coreAudioBuffer, in.mDataFormat.mBytesPerFrame * in.frameCount);
    outQB->mAudioDataByteSize = in.mDataFormat.mBytesPerFrame * in.frameCount;

    AudioQueueEnqueueBuffer(outQ, outQB, 0, NULL);
}


int sound_close_AudioQueue(){

    if( soundInit == 1 )
    {
        AudioQueueDispose(in.queue, true);
        soundInit = 0;
        head = 0;
        tail = 0;
    }
    return 1;
}

int sound_open_AudioQueue(int rate, int bits, int stereo){

    Float64 sampleRate = 48000.0;
    int i;
    UInt32 err;
    int fps;
    int bufferSize;

    if(rate==44100)
        sampleRate = 44100.0;
    if(rate==32000)
        sampleRate = 32000.0;
    else if(rate==22050)
        sampleRate = 22050.0;
    else if(rate==11025)
        sampleRate = 11025.0;

    //SQ Roundup for games like Galaxians
    //fps = ceil(Machine->drv->frames_per_second);
    fps = 60;//TODO

    if( soundInit == 1 )
    {
        sound_close_AudioQueue();
    }

    soundInit = 0;
    memset (&in.mDataFormat, 0, sizeof (in.mDataFormat));
    in.mDataFormat.mSampleRate = sampleRate;
    in.mDataFormat.mFormatID = kAudioFormatLinearPCM;
    in.mDataFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger  | kAudioFormatFlagIsPacked;
    in.mDataFormat.mBytesPerPacket =  (stereo == 1 ? 4 : 2 );
    in.mDataFormat.mFramesPerPacket = 1;
    in.mDataFormat.mBytesPerFrame = (stereo ==  1? 4 : 2);
    in.mDataFormat.mChannelsPerFrame = (stereo == 1 ? 2 : 1);
    in.mDataFormat.mBitsPerChannel = 16;
    in.frameCount = rate / fps;

    err = AudioQueueNewOutput(&in.mDataFormat,
                              AQBufferCallback,
                              NULL,
                              NULL,
                              kCFRunLoopCommonModes,
                              0,
                              &in.queue);

    //printf("res %ld",err);

    bufferSize = in.frameCount * in.mDataFormat.mBytesPerFrame;

    for (i=0; i<AUDIO_BUFFERS; i++)
    {
        err = AudioQueueAllocateBuffer(in.queue, bufferSize, &in.mBuffers[i]);
        in.mBuffers[i]->mAudioDataByteSize = bufferSize;
        AudioQueueEnqueueBuffer(in.queue, in.mBuffers[i], 0, NULL);
    }

    soundInit = 1;
    err = AudioQueueStart(in.queue, NULL);

    return err;
}

///////// AUDIO UNIT
#define kOutputBus 0
static AudioComponentInstance audioUnit;

static OSStatus playbackCallback(void *inRefCon,
                                 AudioUnitRenderActionFlags *ioActionFlags,
                                 const AudioTimeStamp *inTimeStamp,
                                 UInt32 inBusNumber,
                                 UInt32 inNumberFrames,
                                 AudioBufferList *ioData) {
    // Notes: ioData contains buffers (may be more than one!)
    // Fill them up as much as you can. Remember to set the size value in each buffer to match how
    // much data is in the buffer.
    
    unsigned  char *coreAudioBuffer;
    
    int i;
    for (i = 0 ; i < ioData->mNumberBuffers; i++)
    {
        coreAudioBuffer = (unsigned char*) ioData->mBuffers[i].mData;
        //ioData->mBuffers[i].mDataByteSize = dequeue(coreAudioBuffer,inNumberFrames * 4);
        dequeue(coreAudioBuffer,inNumberFrames * 4);
        ioData->mBuffers[i].mDataByteSize = inNumberFrames * 4;
    }
    
    return noErr;
}

int sound_close_AudioUnit(){
    
    if( soundInit == 1 )
    {
        OSStatus status = AudioOutputUnitStop(audioUnit);
        checkStatus(status);
        
        AudioUnitUninitialize(audioUnit);
        soundInit = 0;
        head = 0;
        tail = 0;
    }
    
    return 1;
}

int sound_open_AudioUnit(int rate, int bits, int stereo){
    Float64 sampleRate = 48000.0;

    if( soundInit == 1 )
    {
        sound_close_AudioUnit();
    }
    
    if(rate==44100)
        sampleRate = 44100.0;
    if(rate==32000)
        sampleRate = 32000.0;
    else if(rate==22050)
        sampleRate = 22050.0;
    else if(rate==11025)
        sampleRate = 11025.0;
    
    //audioBufferSize =  (rate / 60) * 2 * (stereo==1 ? 2 : 1) ;
    
    OSStatus status;
    
    // Describe audio component
    AudioComponentDescription desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    
    // Get component
    AudioComponent inputComponent = AudioComponentFindNext(NULL, &desc);
    
    // Get audio units
    status = AudioComponentInstanceNew(inputComponent, &audioUnit);
    checkStatus(status);
    
    UInt32 flag = 1;
    // Enable IO for playback
    status = AudioUnitSetProperty(audioUnit,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Output,
                                  kOutputBus,
                                  &flag,
                                  sizeof(flag));
    checkStatus(status);
    
    AudioStreamBasicDescription audioFormat;
    
    memset (&audioFormat, 0, sizeof (audioFormat));
    
    audioFormat.mSampleRate = sampleRate;
    audioFormat.mFormatID = kAudioFormatLinearPCM;
    audioFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger  | kAudioFormatFlagIsPacked;
    audioFormat.mBytesPerPacket =  (stereo == 1 ? 4 : 2 );
    audioFormat.mFramesPerPacket = 1;
    audioFormat.mBytesPerFrame = (stereo ==  1? 4 : 2);
    audioFormat.mChannelsPerFrame = (stereo == 1 ? 2 : 1);
    audioFormat.mBitsPerChannel = 16;
    
    status = AudioUnitSetProperty(audioUnit,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  kOutputBus,
                                  &audioFormat,
                                  sizeof(audioFormat));
    checkStatus(status);
    
    struct AURenderCallbackStruct callbackStruct;
    // Set output callback
    callbackStruct.inputProc = playbackCallback;
    callbackStruct.inputProcRefCon = NULL;
    status = AudioUnitSetProperty(audioUnit,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Global,
                                  kOutputBus,
                                  &callbackStruct,
                                  sizeof(callbackStruct));
    checkStatus(status);
    
    status = AudioUnitInitialize(audioUnit);
    checkStatus(status);
    
    //ARRANCAR
    soundInit = 1;
    status = AudioOutputUnitStart(audioUnit);
    checkStatus(status);
    
    return 1;
}


