import cv2

BIT_SIZE = 8

FILEPATH = "./origin/parrot.avi" 

video = cv2.VideoCapture(FILEPATH)

frameAll = int(video.get(cv2.CAP_PROP_FRAME_COUNT))
framerate = int(video.get(cv2.CAP_PROP_FPS))

print(frameAll)
print(framerate)

count = 0
while True:
    ret, frame = video.read()
    if not ret:
        break
    print('{')
    for v in range( frame.shape[ 0 ] ):
        print('{',end='')
        for h in range ( frame.shape[ 1 ] ):
            b = frame[v][h][0]
            g = frame[v][h][1]
            r = frame[v][h][2]
            bl = min((b*(BIT_SIZE+1))//255,BIT_SIZE)
            gl = min((g*(BIT_SIZE+1))//255,BIT_SIZE)
            rl = min((r*(BIT_SIZE+1))//255,BIT_SIZE)
            val = (((1<<bl)-1)<<16)|(((1<<gl)-1)<<8)|((1<<rl)-1)
            print('0x{:06X},'.format(val),end='')
        print('},')
    print('},')

