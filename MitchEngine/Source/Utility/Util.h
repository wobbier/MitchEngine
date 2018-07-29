#pragma once

#define MAN_DISABLE_DEFAULT_CONSTRUCTOR(Class)	\
Class() = delete;

#define MAN_DISABLE_COPY_CONSTRUCTOR(Class)		\
Class(const Class&) = delete;

#define MAN_DISABLE_COPY_ASSIGNMENT(Class)		\
Class& operator=(const Class&) = delete;

#define MAN_DISABLE_MOVE_CONSTRUCTOR(Class)		\
Class(Class&&) = delete;

#define MAN_DISABLE_MOVE_ASSIGNMENT(Class)		\
Class& operator=(Class&&) = delete;

#define MAN_NONCOPYABLE(Class) MAN_DISABLE_COPY_CONSTRUCTOR(Class); MAN_DISABLE_COPY_ASSIGNMENT(Class);

#define MAN_NONMOVABLE(Class) MAN_DISABLE_MOVE_CONSTRUCTOR(Class); MAN_DISABLE_MOVE_ASSIGNMENT(Class);