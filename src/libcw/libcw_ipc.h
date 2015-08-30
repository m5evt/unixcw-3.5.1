/*
  This file is a part of unixcw project.
  unixcw project is covered by GNU General Public License.
*/

#ifndef H_LIBCW_IPC
#define H_LIBCW_IPC





#define libcw_sem_post_binary(m_semaphore, m_debug, m_message)		\
	{								\
		int m_val = 0;						\
		int m_ret = sem_getvalue((m_semaphore), &m_val);	\
		if (m_ret != 0) {					\
			fprintf(stderr, "EE: libcw/ipc: %s:%d: libcw_sem_post_binary():sem_getvalue() error: %s\n", __FILE__, __LINE__, strerror(errno)); \
		} else {						\
			if (m_val == 0) {				\
				if (m_debug) {				\
					fprintf(stderr, "%s\n", m_message); \
				}					\
				sem_post((m_semaphore));		\
			}						\
		}							\
	}




#define libcw_sem_flush(m_semaphore)					\
	{								\
		int m_val = 0;						\
		do {							\
			int m_ret = sem_getvalue((m_semaphore), &m_val); \
			if (m_ret != 0) {				\
				fprintf(stderr, "EE: libcw/ipc: %s:%d: libcw_sem_flush():sem_getvalue() error: %s\n", __FILE__, __LINE__, strerror(errno)); \
			} else {					\
				if (m_val) {				\
					sem_wait((m_semaphore));	\
				} else {				\
					break;				\
				}					\
			}						\
		} while (m_val);					\
	}




#endif /* #ifndef H_LIBCW_IPC */
