hello_opengl_oculus
===================

A combination of a few examples into a single file, hopefully cross platform OpenGL Oculus Rift "boilerplate".

Sources used
============

Almost all of this code was taken from other examples, but rearanged a bit to fit some of my needs. These were the projects I used as examples:

- [RiftSkeleton](https://github.com/jimbo00000/RiftSkeleton)
- [Simple, one page, OpenGL example (0.4.3 beta)](https://forums.oculus.com/viewtopic.php?f=20&t=16809)

Most of the cross platform stuff in main.cpp is from RiftSkeleton. Most of the window, texture, and framebuffer code was taken from the simple one page opengl example.

I'm going to use this code as a starting point for larger projects but those projects will require a much more robust architecture than what is currently found here. This pure, functional programming, single page approach is easier for learning the Oculus SDK than trying to understand all of the Object Oriented structures found in RiftSkeleton... or trying to work backwards from the official Oculus demos to get OpenGL working.

That said, this probably has issues. I haven't tested it on any other platforms yet. I'll probably need to make some CMake scripts at some point to get the cross platform stuff working correctly.

But for now, this will be enough to start!
