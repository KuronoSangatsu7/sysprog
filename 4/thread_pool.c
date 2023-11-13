#include "thread_pool.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

struct thread_task
{
	thread_task_f function;
	void *arg;
	void *result;

	int index;
	bool is_finished;
	bool is_running;
	bool detached;

	pthread_cond_t done_cond;
	struct thread_pool *pool;
};

struct thread_pool
{
	pthread_t *threads;
	int max_thread_count;
	int thread_count;

	struct thread_task **tasks;
	int pending_tasks_count;
	int running_tasks_count;
	int pushed_tasks_count;

	pthread_mutex_t lock;
	pthread_cond_t update_cond;

	bool updating;
};

void swap_tasks(struct thread_task *task_1, struct thread_task *task_2)
{
	struct thread_pool *pool = task_1->pool;
	int index_1 = task_1->index;
	int index_2 = task_2->index;

	pool->tasks[index_1] = task_2;
	pool->tasks[index_2] = task_1;

	task_2->index = index_1;
	task_1->index = index_2;
}

void join_thread_task(struct thread_task *task)
{
	struct thread_pool *pool = task->pool;
	while (!task->is_finished)
	{
		pthread_cond_wait(&task->done_cond, &pool->lock);
	}

	pool->pushed_tasks_count--;
	swap_tasks(task, pool->tasks[pool->pushed_tasks_count]);

	task->pool = NULL;
	task->index = -1;
	task->is_running = false;
}

void execute_task(struct thread_task *task_to_execute, struct thread_pool *pool)
{
	void *result = task_to_execute->function(task_to_execute->arg);

	pthread_mutex_lock(&pool->lock);

	task_to_execute->result = result;
	pool->running_tasks_count--;
	pthread_cond_broadcast(&task_to_execute->done_cond);
	task_to_execute->is_finished = true;

	if (task_to_execute->detached)
	{
		join_thread_task(task_to_execute);
		thread_task_delete(task_to_execute);
	}

	pthread_mutex_unlock(&pool->lock);
}

void *thread_func(void *arg)
{
	struct thread_pool *pool = (struct thread_pool *)arg;

	while (true)
	{
		pthread_mutex_lock(&pool->lock);
		while (pool->pending_tasks_count == 0)
		{
			pthread_cond_wait(&pool->update_cond, &pool->lock);

			if (pool->updating == 1)
			{
				pthread_mutex_unlock(&pool->lock);
				return NULL;
			}
		}

		struct thread_task *task_to_execute = pool->tasks[0];

		pool->pending_tasks_count--;
		swap_tasks(task_to_execute, pool->tasks[pool->pending_tasks_count]);

		pool->running_tasks_count++;
		task_to_execute->is_running = true;
		pthread_mutex_unlock(&pool->lock);

		execute_task(task_to_execute, pool);
	}
}

int thread_pool_new(int max_thread_count, struct thread_pool **pool)
{
	if (max_thread_count <= 0 || max_thread_count > TPOOL_MAX_THREADS)
		return TPOOL_ERR_INVALID_ARGUMENT;

	struct thread_pool *new_pool = malloc(sizeof(struct thread_pool));
	*new_pool = (struct thread_pool){
		.threads = malloc(max_thread_count * sizeof(pthread_t)),
		.max_thread_count = max_thread_count,
		.thread_count = 0,

		.tasks = malloc(TPOOL_MAX_TASKS * sizeof(struct thread_task *)),
		.pending_tasks_count = 0,
		.running_tasks_count = 0,

		.updating = -1};

	pthread_mutex_init(&new_pool->lock, NULL);
	pthread_cond_init(&new_pool->update_cond, NULL);

	*pool = new_pool;
	return 0;
}

int thread_pool_thread_count(const struct thread_pool *pool)
{
	pthread_mutex_lock(&((struct thread_pool *)pool)->lock);
	int count = pool->thread_count;
	pthread_mutex_unlock(&((struct thread_pool *)pool)->lock);

	return count;
}

int thread_pool_delete(struct thread_pool *pool)
{
	pthread_mutex_lock(&pool->lock);
	if (pool->pending_tasks_count > 0 || pool->running_tasks_count > 0)
	{
		pthread_mutex_unlock(&pool->lock);
		return TPOOL_ERR_HAS_TASKS;
	}

	pool->updating = 1;
	pthread_cond_broadcast(&pool->update_cond);

	pthread_mutex_unlock(&pool->lock);

	for (int i = 0; i < pool->thread_count; ++i)
	{
		pthread_join(pool->threads[i], NULL);
	}

	pthread_cond_destroy(&pool->update_cond);
	pthread_mutex_destroy(&pool->lock);
	free(pool->tasks);
	free(pool->threads);
	free(pool);

	return 0;
}

int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	pthread_mutex_lock(&pool->lock);

	if (pool->pushed_tasks_count >= TPOOL_MAX_TASKS)
	{
		pthread_mutex_unlock(&pool->lock);
		return TPOOL_ERR_TOO_MANY_TASKS;
	}

	task->is_finished = false;
	task->is_running = false;
	task->pool = pool;

	pool->tasks[pool->pushed_tasks_count++] = task;
	task->index = pool->pushed_tasks_count - 1;
	pool->pending_tasks_count++;

	if (pool->running_tasks_count == pool->thread_count && pool->thread_count < pool->max_thread_count)
	{
		pthread_create(&pool->threads[pool->thread_count++], NULL, thread_func, pool);
	}
	else
	{
		if (pool->pending_tasks_count == 1)
		{
			pool->updating = 0;
			pthread_cond_signal(&pool->update_cond);
		}
	}
	swap_tasks(pool->tasks[pool->pending_tasks_count - 1], task);
	pthread_mutex_unlock(&pool->lock);

	return 0;
}

int thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	struct thread_task *new_task = malloc(sizeof(struct thread_task));
	*new_task = (struct thread_task){
		.function = function,
		.arg = arg,
		.is_finished = false,
		.is_running = false,
		.detached = false,
		.pool = NULL,
		.index = -1};
	pthread_cond_init(&new_task->done_cond, NULL);

	*task = new_task;
	return 0;
}

int thread_task_delete(struct thread_task *task)
{
	if (task->pool != NULL)
		return TPOOL_ERR_TASK_IN_POOL;

	pthread_cond_destroy(&task->done_cond);
	free(task);
	return 0;
}

bool thread_task_is_finished(const struct thread_task *task)
{
	if (task->pool == NULL)
		return task->is_finished;

	pthread_mutex_lock(&task->pool->lock);
	bool is_finished = task->is_finished;
	pthread_mutex_unlock(&task->pool->lock);

	return is_finished;
}

bool thread_task_is_running(const struct thread_task *task)
{
	if (task->pool == NULL)
		return false;

	pthread_mutex_lock(&task->pool->lock);
	bool is_running = task->is_running && !task->is_finished;
	pthread_mutex_unlock(&task->pool->lock);

	return is_running;
}

int thread_task_join(struct thread_task *task, void **result)
{
	struct thread_pool *pool = task->pool;
	if (pool == NULL)
		return TPOOL_ERR_TASK_NOT_PUSHED;

	pthread_mutex_lock(&pool->lock);
	join_thread_task(task);
	*result = task->result;
	pthread_mutex_unlock(&pool->lock);

	return 0;
}

int thread_task_detach(struct thread_task *task)
{
	if (task->pool == NULL)
		return TPOOL_ERR_TASK_NOT_PUSHED;
	struct thread_pool *pool = task->pool;

	pthread_mutex_lock(&pool->lock);
	if (task->is_finished)
	{
		join_thread_task(task);
		thread_task_delete(task);
	}
	else
	{
		task->detached = true;
	}
	pthread_mutex_unlock(&pool->lock);

	return 0;
}

// Constant used for time conversions
const long NSEC_PER_SEC = 1e9;
const long MILSEC_PER_SEC = 1e3;

// Credit to https://github.com/solemnwarning
// for the following functions
// https://github.com/solemnwarning/timespec/blob/master/timespec.c
// ---------------------------------------------------------------------
struct timespec timespec_normalise(struct timespec ts)
{
	while (ts.tv_nsec >= NSEC_PER_SEC)
	{
		++(ts.tv_sec);
		ts.tv_nsec -= NSEC_PER_SEC;
	}

	while (ts.tv_nsec <= -NSEC_PER_SEC)
	{
		--(ts.tv_sec);
		ts.tv_nsec += NSEC_PER_SEC;
	}

	if (ts.tv_nsec < 0)
	{
		--(ts.tv_sec);
		ts.tv_nsec = (NSEC_PER_SEC + ts.tv_nsec);
	}

	return ts;
}

struct timespec timespec_add(struct timespec ts1, struct timespec ts2)
{
	ts1 = timespec_normalise(ts1);
	ts2 = timespec_normalise(ts2);

	ts1.tv_sec += ts2.tv_sec;
	ts1.tv_nsec += ts2.tv_nsec;

	return timespec_normalise(ts1);
}

struct timespec timespec_from_ms(long milliseconds)
{
	struct timespec ts = {
		.tv_sec = (milliseconds / 1000),
		.tv_nsec = (milliseconds % 1000) * 1000000,
	};

	return timespec_normalise(ts);
}

struct timespec timespec_from_timeval(struct timeval tv)
{
	struct timespec ts = {
		.tv_sec = tv.tv_sec,
		.tv_nsec = tv.tv_usec * 1000};

	return timespec_normalise(ts);
}
// ---------------------------------------------------------------------

struct timespec timespec_now()
{
	struct timeval now;

	gettimeofday(&now, NULL);
	return timespec_from_timeval(now);
}

int thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	if (task->pool == NULL)
	{
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}

	struct thread_pool *pool = task->pool;

	pthread_mutex_lock(&pool->lock);

	struct timespec timeout_spec = timespec_add(
		timespec_now(),
		timespec_from_ms((long long)(timeout * (double)MILSEC_PER_SEC)));

	while (!task->is_finished)
	{
		if (ETIMEDOUT == pthread_cond_timedwait(&task->done_cond, &pool->lock, &timeout_spec))
		{
			pthread_mutex_unlock(&pool->lock);
			return TPOOL_ERR_TIMEOUT;
		}
	}

	pool->pushed_tasks_count--;
	swap_tasks(task, pool->tasks[pool->pushed_tasks_count]);

	task->pool = NULL;
	task->index = -1;
	task->is_running = false;

	*result = task->result;
	pthread_mutex_unlock(&pool->lock);

	return 0;
}