package xcodeml.c.decompile;

import xcodeml.util.XmException;
import xcodeml.c.util.XmcWriter;

/**
 * Implementing this interface allows an object to be appended by XmWriter.
 */
public interface XcAppendable
{
    /**
     * Allows writer to append the object.
     *
     * @param w writer.
     */
    public void appendCode(XmcWriter w) throws XmException;
}
