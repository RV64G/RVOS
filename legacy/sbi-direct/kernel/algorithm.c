#include "kernel.h"
timer *insert_to_timer_list(timer *timer_head, timer *_timer)
{
    if (timer_head == NULL)
    {
        next_timer = _timer;
        timer_load(_timer->timeout_tick);
        return _timer;
    }
    if (timer_head->timeout_tick > _timer->timeout_tick)
    {
        _timer->next = timer_head;
        next_timer = _timer;
        timer_load(_timer->timeout_tick);
        return _timer;
    }

    timer *current_timer = timer_head;
    timer_load(timer_head->timeout_tick);
    while (current_timer->next != NULL)
    {
        if (current_timer->next->timeout_tick > _timer->timeout_tick && current_timer->timeout_tick < _timer->timeout_tick)
        {
            _timer->next = current_timer->next;
            current_timer->next = _timer;

            if (_timer->timeout_tick < next_timer->timeout_tick)
            {
                next_timer = _timer;
            }
            return timer_head;
        }
        current_timer = current_timer->next;
    }
    current_timer->next = _timer;
    if (next_timer == NULL || (_timer->timeout_tick < next_timer->timeout_tick))
    {
        next_timer = _timer;
    }
    timer_load(timer_head->timeout_tick);

    return timer_head;
}

timer *delete_from_timer_list(timer *timer_head, timer *_timer)
{
    if (timer_head == NULL)
        return NULL;
    if (timer_head == _timer)
    {
        timer *new_head = timer_head->next;
        if (next_timer == timer_head)
        {
            next_timer = new_head;
        }
        return new_head;
    }
    timer *current_timer = timer_head;
    while (current_timer->next != NULL)
    {
        if (current_timer->next == _timer)
        {
            current_timer->next = current_timer->next->next;
            if (next_timer == _timer)
            {
                next_timer = current_timer->next;
            }
            return timer_head;
        }
        current_timer = current_timer->next;
    }
    return timer_head;
}
