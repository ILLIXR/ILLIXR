import random

class Slam3:
    def __init__(self):
        self.pos = 0

    def feed_accel_nonbl(self):
        pass

    def feed_cam_frame_nonbl(self):
        pass

    def produce_nonbl(self):
        self.pos -= random.randint(0, 10)
        return self.pos

slam3 = Slam3()
