/*
 * Copyright (c) 2023, Azul Systems, Inc. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

import java.util.concurrent.CountDownLatch;

/**
 * @test
 * @bug 6995195
 * @summary Verify that concurrent classloading of GraphicsPrimitiveMgr
 *          and Blit doesn't deadlock
 * @run main/othervm/timeout=20 GraphicsPrimitiveMgrTest
 */
public class GraphicsPrimitiveMgrTest {

    private static volatile CountDownLatch latch;

    private static final String C1 = "sun.java2d.loops.GraphicsPrimitiveMgr";
    private static final String C2 = "sun.java2d.loops.Blit";

    public static void main(final String[] args)
        throws ClassNotFoundException, InterruptedException
    {
        // force loading awt library
        Class.forName("java.awt.Toolkit");

        latch = new CountDownLatch(2);

        Thread t1 = new Thread(() -> loadClass(C1));
        Thread t2 = new Thread(() -> loadClass(C2));

        t1.start();
        t2.start();

        t1.join();
        t2.join();
    }

    private static void loadClass(String className) {
        System.out.println(Thread.currentThread().getName() + " loading " + className);
        try {
            latch.countDown();
            latch.await();
            Class.forName(className);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
