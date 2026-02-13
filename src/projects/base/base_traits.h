#pragma once

#include <modules/physical_port/physical_port_manager.h>

#include <vector>
#include <algorithm>

template<typename T, typename Observer>
class ObservableBase
{
public:
    bool AddObserver(const std::shared_ptr<Observer> &observer)
    {
        if (std::find(observers_.begin(), observers_.end(), observer) != observers_.end())
        {
            logw(T::ClassName, "%p is already observer of %s", observer.get(), T::ClassName);
            return false;
        }

        observers_.push_back(observer);

        return true;
    }

    bool RemoveObserver(const std::shared_ptr<Observer> &observer)
    {
        auto item = std::find(observers_.begin(), observers_.end(), observer);
        if(item == observers_.end())
        {
            logw(T::ClassName, "%p is not a registered observer", observers_.get());
            return false;
        }

        observers_.erase(item);

        return true;
    }

protected:
    std::vector<std::shared_ptr<Observer>> observers_;
};

template<typename T, ov::SocketType socket_type>
class ServerBase : protected PhysicalPortObserver
{
public:
    bool Start(const ov::SocketAddress &address)
    {
        logt(T::ClassName, "%s Start", T::ClassName);

        if(_physical_port != nullptr)
        {
            logw(T::ClassName, "Failed to start %s", T::ClassName);
            return false;
        }

        _physical_port = PhysicalPortManager::GetInstance()->CreatePort(T::ClassName, socket_type, address);

        if(_physical_port != nullptr)
        {
            _physical_port->AddObserver(this);
        }

        return _physical_port != nullptr;
    }

    bool Stop()
    {
        if(_physical_port == nullptr)
        {
            return false;
        }

        _physical_port->RemoveObserver(this);
        PhysicalPortManager::GetInstance()->DeletePort(_physical_port);
        _physical_port = nullptr;

        return true;
    }

protected:
    std::shared_ptr<PhysicalPort> _physical_port;
};
