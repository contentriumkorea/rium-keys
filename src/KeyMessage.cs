namespace AdobeKoreanShortcuts;
public static class KeyMessage
{
    public static nint Pack(uint scan,bool extended,bool up,bool repeat)
    {
        uint bits=1|((scan&0xff)<<16);
        if(extended)bits|=1u<<24;
        if(up||repeat)bits|=1u<<30;
        if(up)bits|=1u<<31;
        return unchecked((nint)(int)bits);
    }
}
