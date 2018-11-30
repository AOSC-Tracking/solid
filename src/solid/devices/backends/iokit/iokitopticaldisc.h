/*
    Copyright 2017 René J.V. Bertin <rjvbertin@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) version 3, or any
    later version accepted by the membership of KDE e.V. (or its
    successor approved by the membership of KDE e.V.), which shall
    act as a proxy defined in Section 6 of version 3 of the license.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SOLID_BACKENDS_IOKIT_OPTICALDISC_H
#define SOLID_BACKENDS_IOKIT_OPTICALDISC_H

#include <solid/devices/ifaces/opticaldisc.h>
#include "iokitvolume.h"

namespace Solid
{
namespace Backends
{
namespace IOKit
{
class IOKitOpticalDisc : public IOKitVolume, virtual public Solid::Ifaces::OpticalDisc
{
    Q_OBJECT
    Q_INTERFACES(Solid::Ifaces::OpticalDisc)

public:
    IOKitOpticalDisc(IOKitDevice *device);
    IOKitOpticalDisc(const IOKitDevice *device);
    virtual ~IOKitOpticalDisc();

    // overridden from IOKit::Block because optical discs must
    // be accessed through the raw device.
    QString device() const override;

    Solid::OpticalDisc::ContentTypes availableContent() const override;
    Solid::OpticalDisc::DiscType discType() const override;
    bool isAppendable() const override;
    bool isBlank() const override;
    bool isRewritable() const override;
    qulonglong capacity() const override;
};
}
}
}

#endif // SOLID_BACKENDS_IOKIT_OPTICALDISC_H
