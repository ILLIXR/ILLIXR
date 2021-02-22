**This is part of [ILLIXR][10], the Illinios Extended Reality Benchmark Suite.
The following explains how to use RITnet.
The code is based on Python3, and the profiling results are based on ```test.py```.
For the testing images, the size per image should be `640x400` in gray scale.
Please put them under ```Semantic_Segmentation_Dataset/test/images```.**

# RITnet

RITnet is the winnning model of the OpenEDS Semantic Segmentation Challenge.
If you use this code, please cite:
```
@misc{chaudhary2019ritnet,
    title={RITnet: Real-time Semantic Segmentation of the Eye for Gaze Tracking},
    author={Aayush K. Chaudhary and Rakshit Kothari and Manoj Acharya and Shusil Dangi and Nitinraj Nair and Reynold Bailey and Christopher Kanan and Gabriel Diaz and Jeff B. Pelz},
    year={2019},
    eprint={1910.00694},
    archivePrefix={arXiv},
    primaryClass={cs.CV}
}
```

Instructions:

```python train.py --help```

To train the model with densenet model:
 
```python train.py --model densenet --expname FINAL --bs 8 --useGPU True --dataset Semantic_Segmentation_Dataset/```

To test the result:
 
```python test.py --model densenet --load best_model.pkl --bs 4 --dataset Semantic_Segmentation_Dataset/```

If you type in ```python test.py```, the batch size will be 8.

## Contents in the zip folder
```
best_model.pkl     :: Our final model (potential winner model) which contains all the weights in Float32 format (Number of Parameters 248900).
requirements.txt   :: Includes all the necessary packages for the source code to run 
environment.yml    :: List of all packages and version of one of our system in which the code was run successfully. 
dataset.py	::Data loader and augmentation
train.py	::Train code
test.py		::Test code
densenet.py	::Model code
utils.py	::List of utility files
opt.py 		::List of arguments for argparser
models.py 	::List of all models
starburst_black.png:: A fixed structured pattern (with translation) used on train images to handle cases such as multiple reflections.(Train Image: 000000240768.png)
Starburst generation from train image 000000240768.pdf  ::Procedure how starburst pattern is generated
```


The requirements.txt file contains all the packages necessary for the code to run.
We have also included an environment.yml file to recreate the conda environment we used.

We have submitted two models from this version of code:

1.  Epoch: 151 Validation accuracy: 95.7780  Test accuracy: 95.276  (Potential Winner Model:
        Last Submission)
2.  Epoch: 117 Validation accuracy: 95.7023  Test accuracy: 95.159  (Our Second Last Submission)

We could reach upto
Epoch: 240 Validation accuracy: 95.7820 Test accuracy:NA (Not submitted: result after the deadline)


The dataset.py contains data loader, preprocessing and post processing step
Required Preprocessing for all images (test, train and validation set).

1.  Gamma correction by a factor of 0.8
2.  local Contrast limited adaptive histogram equalization algorithm
        with clipLimit=1.5, tileGridSize=(8,8)
3.  Normalization [Mean 0.5, std=0.5]
    
Train Image Augmentation Procedure Followed (Not Required during test)

1.  Random horizontal flip with 50% probability.
2.  Starburst pattern augmentation with 20% probability. 
3.  Random length lines (1 to 9) augmentation around a random center with 20% probability. 
4.  Gaussian blur with kernel size (7,7) and random sigma (2 to 7) with 20% probability. 
5.  Translation of image and labels in any direction with random factor less than 20
        with 20% probability.

```
----------------------------------------------------------------
        Layer (type)               Output Shape         Param #
================================================================
            Conv2d-1         [-1, 32, 640, 400]             320
           Dropout-2         [-1, 32, 640, 400]               0
         LeakyReLU-3         [-1, 32, 640, 400]               0
            Conv2d-4         [-1, 32, 640, 400]           1,088
            Conv2d-5         [-1, 32, 640, 400]           9,248
           Dropout-6         [-1, 32, 640, 400]               0
         LeakyReLU-7         [-1, 32, 640, 400]               0
            Conv2d-8         [-1, 32, 640, 400]           2,112
            Conv2d-9         [-1, 32, 640, 400]           9,248
          Dropout-10         [-1, 32, 640, 400]               0
        LeakyReLU-11         [-1, 32, 640, 400]               0
      BatchNorm2d-12         [-1, 32, 640, 400]              64
DenseNet2D_down_block-13         [-1, 32, 640, 400]               0
        AvgPool2d-14         [-1, 32, 320, 200]               0
           Conv2d-15         [-1, 32, 320, 200]           9,248
          Dropout-16         [-1, 32, 320, 200]               0
        LeakyReLU-17         [-1, 32, 320, 200]               0
           Conv2d-18         [-1, 32, 320, 200]           2,080
           Conv2d-19         [-1, 32, 320, 200]           9,248
          Dropout-20         [-1, 32, 320, 200]               0
        LeakyReLU-21         [-1, 32, 320, 200]               0
           Conv2d-22         [-1, 32, 320, 200]           3,104
           Conv2d-23         [-1, 32, 320, 200]           9,248
          Dropout-24         [-1, 32, 320, 200]               0
        LeakyReLU-25         [-1, 32, 320, 200]               0
      BatchNorm2d-26         [-1, 32, 320, 200]              64
DenseNet2D_down_block-27         [-1, 32, 320, 200]               0
        AvgPool2d-28         [-1, 32, 160, 100]               0
           Conv2d-29         [-1, 32, 160, 100]           9,248
          Dropout-30         [-1, 32, 160, 100]               0
        LeakyReLU-31         [-1, 32, 160, 100]               0
           Conv2d-32         [-1, 32, 160, 100]           2,080
           Conv2d-33         [-1, 32, 160, 100]           9,248
          Dropout-34         [-1, 32, 160, 100]               0
        LeakyReLU-35         [-1, 32, 160, 100]               0
           Conv2d-36         [-1, 32, 160, 100]           3,104
           Conv2d-37         [-1, 32, 160, 100]           9,248
          Dropout-38         [-1, 32, 160, 100]               0
        LeakyReLU-39         [-1, 32, 160, 100]               0
      BatchNorm2d-40         [-1, 32, 160, 100]              64
DenseNet2D_down_block-41         [-1, 32, 160, 100]               0
        AvgPool2d-42           [-1, 32, 80, 50]               0
           Conv2d-43           [-1, 32, 80, 50]           9,248
          Dropout-44           [-1, 32, 80, 50]               0
        LeakyReLU-45           [-1, 32, 80, 50]               0
           Conv2d-46           [-1, 32, 80, 50]           2,080
           Conv2d-47           [-1, 32, 80, 50]           9,248
          Dropout-48           [-1, 32, 80, 50]               0
        LeakyReLU-49           [-1, 32, 80, 50]               0
           Conv2d-50           [-1, 32, 80, 50]           3,104
           Conv2d-51           [-1, 32, 80, 50]           9,248
          Dropout-52           [-1, 32, 80, 50]               0
        LeakyReLU-53           [-1, 32, 80, 50]               0
      BatchNorm2d-54           [-1, 32, 80, 50]              64
DenseNet2D_down_block-55           [-1, 32, 80, 50]               0
        AvgPool2d-56           [-1, 32, 40, 25]               0
           Conv2d-57           [-1, 32, 40, 25]           9,248
          Dropout-58           [-1, 32, 40, 25]               0
        LeakyReLU-59           [-1, 32, 40, 25]               0
           Conv2d-60           [-1, 32, 40, 25]           2,080
           Conv2d-61           [-1, 32, 40, 25]           9,248
          Dropout-62           [-1, 32, 40, 25]               0
        LeakyReLU-63           [-1, 32, 40, 25]               0
           Conv2d-64           [-1, 32, 40, 25]           3,104
           Conv2d-65           [-1, 32, 40, 25]           9,248
          Dropout-66           [-1, 32, 40, 25]               0
        LeakyReLU-67           [-1, 32, 40, 25]               0
      BatchNorm2d-68           [-1, 32, 40, 25]              64
DenseNet2D_down_block-69           [-1, 32, 40, 25]               0
           Conv2d-70           [-1, 32, 80, 50]           2,080
           Conv2d-71           [-1, 32, 80, 50]           9,248
          Dropout-72           [-1, 32, 80, 50]               0
        LeakyReLU-73           [-1, 32, 80, 50]               0
           Conv2d-74           [-1, 32, 80, 50]           3,104
           Conv2d-75           [-1, 32, 80, 50]           9,248
          Dropout-76           [-1, 32, 80, 50]               0
        LeakyReLU-77           [-1, 32, 80, 50]               0
DenseNet2D_up_block_concat-78           [-1, 32, 80, 50]               0
           Conv2d-79         [-1, 32, 160, 100]           2,080
           Conv2d-80         [-1, 32, 160, 100]           9,248
          Dropout-81         [-1, 32, 160, 100]               0
        LeakyReLU-82         [-1, 32, 160, 100]               0
           Conv2d-83         [-1, 32, 160, 100]           3,104
           Conv2d-84         [-1, 32, 160, 100]           9,248
          Dropout-85         [-1, 32, 160, 100]               0
        LeakyReLU-86         [-1, 32, 160, 100]               0
DenseNet2D_up_block_concat-87         [-1, 32, 160, 100]               0
           Conv2d-88         [-1, 32, 320, 200]           2,080
           Conv2d-89         [-1, 32, 320, 200]           9,248
          Dropout-90         [-1, 32, 320, 200]               0
        LeakyReLU-91         [-1, 32, 320, 200]               0
           Conv2d-92         [-1, 32, 320, 200]           3,104
           Conv2d-93         [-1, 32, 320, 200]           9,248
          Dropout-94         [-1, 32, 320, 200]               0
        LeakyReLU-95         [-1, 32, 320, 200]               0
DenseNet2D_up_block_concat-96         [-1, 32, 320, 200]               0
           Conv2d-97         [-1, 32, 640, 400]           2,080
           Conv2d-98         [-1, 32, 640, 400]           9,248
          Dropout-99         [-1, 32, 640, 400]               0
       LeakyReLU-100         [-1, 32, 640, 400]               0
          Conv2d-101         [-1, 32, 640, 400]           3,104
          Conv2d-102         [-1, 32, 640, 400]           9,248
         Dropout-103         [-1, 32, 640, 400]               0
       LeakyReLU-104         [-1, 32, 640, 400]               0
DenseNet2D_up_block_concat-105         [-1, 32, 640, 400]               0
         Dropout-106         [-1, 32, 640, 400]               0
          Conv2d-107          [-1, 4, 640, 400]             132
================================================================
Total params: 248,900
Trainable params: 248,900
Non-trainable params: 0
----------------------------------------------------------------
Input size (MB): 0.98
Forward/backward pass size (MB): 1920.41
Params size (MB): 0.95
Estimated Total Size (MB): 1922.34
----------------------------------------------------------------

```


[//]: # (- Internal -)

[10]:   index.md
