#pragma once

#define MA_DISABLE_DEFAULT_CONSTRUCTOR(Class)	\
Class() = delete;

#define MA_DISABLE_COPY_CONSTRUCTOR(Class)		\
Class(const Class&) = delete;					

#define MA_DISABLE_COPY_ASSIGNMENT(Class)		\
Class& operator=(const Class&) = delete;

#define MA_DISABLE_MOVE_CONSTRUCTOR(Class)		\
Class(Class&&) = delete;

#define MA_DISABLE_MOVE_ASSIGNMENT(Class)		\
Class& operator=(Class&&) = delete;

#define MA_NONCOPYABLE(Class) MA_DISABLE_COPY_CONSTRUCTOR(Class); MA_DISABLE_COPY_ASSIGNMENT(Class);

#define MA_NONMOVABLE(Class) MA_DISABLE_MOVE_CONSTRUCTOR(Class); MA_DISABLE_MOVE_ASSIGNMENT(Class);