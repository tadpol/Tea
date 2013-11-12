/**
 * \file tea.h
 *
 */
#ifndef __TEA_H__
#define __TEA_H__

#ifdef __LP64__
typedef unsigned long teaint; /*!< a 64 bit number */
#else
typedef unsigned int teaint; /*!< a 32 bit number */
#endif
typedef unsigned short teashort; /*!< a 16 bit number */
typedef unsigned char teabyte; /*!< a 8 bit number */


/**
 * \brief How deep is the stack
 */
#define tea_stack_depth 10


void tea_push(teaint t);
teaint tea_pop(void);


#ifdef TEA_INTERNAL_USAGE  /* Only tea.c should define this. */
struct tea_ptr_alias_s {
    char *alias;
    void *ptr;
} tea_pa_table[] = {

    {NULL,NULL} /* MUST BE the last entry */
};
#endif

#endif /*__TEA_H__*/
/* vim: set ai cin et sw=4 ts=4 : */
