#include <linux/sched/prio.h> 				//we need MAX_PRIO macro
#include <linux/rbtree.h>				//we need rb-trees to get the earliest deadline task!
#include <linux/list.h>					//we need lists to keep the running tasks inside ss_rq
#include "ss_debug.h"					//debug macros , example -> ss_debug(...)
#include "sched.h"					//we definetely need ss_task and ss_rq definitions , right?
#include "ss.h"

extern struct ss_task*alloc_ss_task(struct task_struct *p); // @see ss_init.c

/*
ss_utill_task_is_dead(struct ss_task *ss_task)
[t]
returns 1 if task must remove from linked list of tasks , 0 if not
@param ss_task the ss_task of the task to examine  @see sched.h/struct ss_task
*/
extern int ss_utill_task_is_dead(struct ss_task *ss_task){
	return ss_task->task->state==TASK_DEAD||
		ss_task->task->state==EXIT_DEAD||
		ss_task->task->state==EXIT_ZOMBIE;


}
SS_EXPORT_IF_DEBUG(ss_utill_task_is_dead);
/*
struct ss_task *find_ss_task (struct ss_rq *ss_rq,struct task_struct *p)
[*]
@brief Search and return the ss_task struct of the task_struct * given on runqueue given
@note  Just iterate over ss_rq->ss_list(i) , if if(i->task==p) return p , otherwise return NULL (meaning that the
task_struct given has not STEF_SCHED policy! :))
@return an struct ss_task , with ss_task->task==p
	NULL otherwise
@used_in
	*)enqueue_task_ss (ss.c)
	*)dequeue_task_ss (ss.c)

[t]
*/
extern struct ss_task *find_ss_task(struct ss_rq *ss_rq,struct task_struct *p){
	struct list_head*htmp;			//temponary variable to each list node
	struct ss_task  *tmp; 			//temponary var , to iterate over each element
	list_for_each(htmp,&ss_rq->ss_list) {
		tmp=list_entry(htmp,struct ss_task,ss_list_node);
		if(tmp->task==p)return tmp;
	}
	return NULL;

}
SS_EXPORT_IF_DEBUG(find_ss_task);
/*
struct ss_task *get_earliest_ss_task(struct ss_rq*)
[*]
@version 0.0.1
@brief picks the leftmost node(with earliest deadline ;) )
@param struct ss_rq , the currenly runqueue
*/
extern struct ss_task *get_earliest_ss_task(struct ss_rq*ss_rq){
	struct rb_node *temp = ss_rq->ss_root.rb_node;
	if(atomic_read(&ss_rq->nr_running)==0||
				temp==NULL)return NULL;				//if no tasks exist in runqueue , then return NULL
	while(temp->rb_left)temp=temp->rb_left;					//traverse the list untill the left node is null
	return (struct ss_task*)rb_entry(temp,struct ss_task,ss_node);		//return the task existed there! :)
}
/*
int insert_ss_task_rb_tree(struct ss_rq*,struct ss_task*)
[*]
@version 0.0.2
Imports a new task into sscheduler's red-black tree!
@param struct ss_rq  * ss_rq   -> The current runqueue
@param struct ss_task* ss_task -> The process to insert in rbtree!
@returns >1 if all okay , 0 otherwise
Revisions
version_before  version_after           brief                   solution                status
0.0.1           0.0.2                   known kernel bug     	quick and dirty		FIX
								fix of known kernel
								bug of infinite loop
								on rb_insert_color()
								insert an flags param

*/
SS_EXPORT_IF_DEBUG(get_earliest_ss_task);
extern int insert_ss_task_rb_tree (struct ss_rq*ss_rq,struct ss_task*ss_task,int flags){
	struct rb_node ** temp  = &(ss_rq->ss_root.rb_node) ;	//At first , temp node starts from root node!
	struct rb_node * parent= NULL;				//..and of course , has no parent :P
	struct ss_task * temp_container=NULL;			//temp's container
	ss_debug("insert_ss_task_rb_tree on rq(%px) on ss_task(%px) with flag %s on initial_root (%px)",
												ss_rq,
												ss_task,
												flags,
												temp);
	while(*temp) {
		temp_container=container_of(*temp,struct ss_task,ss_node);
		parent=*temp;
		if(ss_task->absolute_deadline< temp_container->absolute_deadline){
			ss_debug("LEFT");
			temp=&(*temp)->rb_left;
		}
		else if(ss_task->absolute_deadline > temp_container->absolute_deadline){
			ss_debug("RIGHT");
			temp=&(*temp)->rb_right;
		}
		else return 0;
	}
	rb_link_node(&ss_task->ss_node,parent,temp);		//insert into rbtree
	if(flags&SS_ALLOW_RECOLOR){
		rb_insert_color(&ss_task->ss_node,&ss_rq->ss_root);	//rebalance if nessesary
	}
	return 1;
}
SS_EXPORT_IF_DEBUG(insert_ss_task_rb_tree);
/*
int remove_ss_task_rb_tree(struct ss_rq*ss_rq,struct ss_task *ss_task)
[t]
Removes a task from red-black tree :O
@param struct ss_rq  *ss_rq , the current runqueue
@param struct ss_task*ss_task ,the process to remove from red-black tree
@return 1 on success , 0 otherwise
*/
extern int remove_ss_task_rb_tree(struct ss_rq*ss_rq,struct ss_task*ss_task){
	rb_erase(&ss_task->ss_node,&ss_rq->ss_root);
	return 1;

}
SS_EXPORT_IF_DEBUG(remove_ss_task_rb_tree);

/*
extern int remove_ss_task_rq_list(struct ss_rq*ss_rq,struct ss_task*ss_task)
[t]
removes an ss_task from runqueue
@param 		ss_rq 		the current runqueue
@param		ss_task		the task to remove
@returns	1)0 in success
		2)EINVAL if this ss_task not belong to this runqueue
TODO:refactor so to use find_ss_task instead of looping (the @param ss_task must changed to task_struct)
*/
extern int remove_ss_task_rq_list(struct ss_rq*ss_rq,struct ss_task*ss_task){
	struct list_head *tmp;
	struct ss_task	 *cursor=NULL;
	list_for_each(tmp,&ss_rq->ss_list){
		cursor=list_entry(tmp,struct ss_task,ss_list_node);
		if(cursor==ss_task){
			list_del(&ss_task->ss_list_node);
			return 0;
		}
	}
	return EINVAL;
}
/*
insert_ss_task_rq_list(struct ss_rq*ss_rq,struct task_struct *ss_task)
[*]
adds a new task_struct into runqueue
@param 		ss_rq 	 	the current runqueue
@param		ss_task		the task to add in current runqueue
@Note : Be careful , this routine put the task in runqueue , no in rbtree!
@returns 	1)0 in success
		2)ENOMEM if alloc_ss_task returns NULL (cause of NULL in kmalloc() )*/

extern int insert_ss_task_rq_list(struct ss_rq*ss_rq,struct task_struct*ss_task,const struct sched_attr*attr){
	struct ss_task *ptr = alloc_ss_task(ss_task);
	if(!ptr)return -ENOMEM;
	ptr->task=ss_task;					//assign struct task_struct on ss_task->task
	ptr->absolute_deadline=attr->deadline;
	ptr->task->deadline=0;					//TODO: find a way to pass the relative deadline!
	list_add(&ptr->ss_list_node,&ss_rq->ss_list);
	ss_debug("inserted task:%px on runqueue:%px  with deadline %d ",ss_task,ss_rq,attr->deadline);
        return 0;
}
/*
extern int ss_prio(int prio)
[*]
checks if the given prio belongs to sscheduler
@param prio , the priority to check!
@Note : The priority of sscheduler , is the deadline of the task . because we cant
touch sched_param struct due to ABI compatibillity with user space compiled programms ,
we use sched_param->sched_priority as deadline.
We know that every task (except ss_tasks) have at most MAX_PRIO priority . so
we use the numbers above MAX_PRIO as deadline
The actual deadline of a ss_task with priority X is ...
				deadline(X)=X-MAX_PRIO*/
extern int ss_prio(int prio){
	return prio>MAX_PRIO;
}


