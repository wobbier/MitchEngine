#pragma once

#define ME_DISABLE_DEFAULT_CONSTRUCTOR(Class)	\
Class() = delete;

#define ME_DISABLE_COPY_CONSTRUCTOR(Class)		\
Class(const Class&) = delete;

#define ME_DISABLE_COPY_ASSIGNMENT(Class)		\
Class& operator=(const Class&) = delete;

#define ME_DISABLE_MOVE_CONSTRUCTOR(Class)		\
Class(Class&&) = delete;

#define ME_DISABLE_MOVE_ASSIGNMENT(Class)		\
Class& operator=(Class&&) = delete;

#define ME_NONCOPYABLE(Class) ME_DISABLE_COPY_CONSTRUCTOR(Class); ME_DISABLE_COPY_ASSIGNMENT(Class);

#define ME_NONMOVABLE(Class) ME_DISABLE_MOVE_CONSTRUCTOR(Class); ME_DISABLE_MOVE_ASSIGNMENT(Class);