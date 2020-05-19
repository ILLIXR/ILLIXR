## Audio pipeline

`audio_pipeline` subscribes to the most recent pose, does ambisonics encoding, spatial zoom and rotation according to the pose, ambisonics decoding and binauralization to output a block of 1024 sound samples each time at 48000 Hz sample rate. Therefore it has a 21.3ms period to process each block. If it misses a deadline, it keeps doing its current work for the next deadline.

Currently this component is for profiling purpose only. It does read a pose from illixr, but the pose is not used by spaitial zoom and rotation. Performance-wise, the audio pipeline is input invariant. 
