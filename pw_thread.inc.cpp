// Implementation yoinked from `pipewire-jack.c`

#include <spa/support/thread.h>

static struct spa_thread *impl_create(void *object,
	const struct spa_dict *props,
	void *(*start)(void*), void *arg)
{
	PwHelper::Helper *helper = reinterpret_cast<PwHelper::Helper *>(object);
	struct spa_dict_item *items;
	struct spa_dict copy;
	char creator_ptr[64];

	if (helper->thread_creator) {
		uint32_t i, n_items = props ? props->n_items : 0;

		items = reinterpret_cast<struct spa_dict_item *>(alloca((n_items + 1) * sizeof(*items)));

		for (i = 0; i < n_items; i++)
			items[i] = props->items[i];

		snprintf(creator_ptr, sizeof(creator_ptr), "pointer:%p", helper->thread_creator);
		items[n_items++] = SPA_DICT_ITEM_INIT(SPA_KEY_THREAD_CREATOR,
				creator_ptr);

		copy = SPA_DICT_INIT(items, n_items);
		props = &copy;
	}
	return spa_thread_utils_create(helper->thread_impl, props, start, arg);
}

static int impl_join(void *object,
		struct spa_thread *thread, void **retval)
{
	PwHelper::Helper *helper = reinterpret_cast<PwHelper::Helper *>(object);
	return spa_thread_utils_join(helper->thread_impl, thread, retval);
}

static int impl_acquire_rt(void *object, struct spa_thread *thread, int priority)
{
	PwHelper::Helper *helper = reinterpret_cast<PwHelper::Helper *>(object);
	return spa_thread_utils_acquire_rt(helper->thread_impl, thread, priority);
}

static int impl_drop_rt(void *object, struct spa_thread *thread)
{
	PwHelper::Helper *helper = reinterpret_cast<PwHelper::Helper *>(object);
	return spa_thread_utils_drop_rt(helper->thread_impl, thread);
}

static struct spa_thread_utils_methods thread_utils_impl = {
	.version = SPA_VERSION_THREAD_UTILS_METHODS,
	.create = impl_create,
	.join = impl_join,
	.acquire_rt = impl_acquire_rt,
	.drop_rt = impl_drop_rt,
};
