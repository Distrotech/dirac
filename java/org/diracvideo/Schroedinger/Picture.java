package org.diracvideo.Schroedinger;
import java.awt.*;
import java.awt.image.*;


class SubBand {
    int qi, level, stride, offset, orient, numX, numY;
    Buffer buf;
    Dimension frame, block;
    Parameters par;
    public SubBand (Buffer b, int q, Parameters p) {
	par = p;
	qi = q;
	buf = b;
    }
    
    public void calculateSizes(int i, boolean luma) {
	level = (i-1)/3;
	frame = luma ? par.iwtLumaSize : par.iwtChromaSize;
	orient = (i - 1) % 3 + 1;
	stride = (1 << (par.transformDepth - level));
	offset = (orient == 0 ? 0 : 
		  (orient == 1 ? stride >> 1 :
		   (orient == 2 ? (frame.width * stride) >> 1 :
		    (stride + frame.width * stride) >> 1)));
	numX = (orient == 0 ? par.horiz_codeblocks[0] :
		par.horiz_codeblocks[level+1]);
	numY = (orient == 0 ? par.vert_codeblocks[0] :
		par.vert_codeblocks[level+1]);
    }
    
    public void decodeCoeffs(short[] out) {
	if(buf == null)
	    return;
	Unpack u = new Unpack(buf);
	if(numX * numY == 1) {
	    decodeCodeBlock(out,u,0,0);
	    return;
	}
	for(int y = 0; y < numY; y++) {
	    for(int x = 0; x < numX; x++) {
		if(u.decodeBool())
		    continue;
		if(par.codeblock_mode_index != 0)
		    qi += u.decodeSint();
		decodeCodeBlock(out,u,x,y);
	    }
	} 
    }


    private void decodeCodeBlock(short[] out, Unpack u,
				 int blockX, int blockY) {
	int qo = quantOffset(qi);
	int qf = quantFactor(qi);
	int startX = (frame.width * blockX)/numX;
	int startY = (frame.height * blockY)/numY;
	int endX = (frame.width * (blockX+1))/numX;
	int endY = (frame.height * (blockY+1))/numY;
	int blockWidth = endX - startX;
	int blockEnd = (frame.width*(endY-1)) + endX;
	int blockOffset = (frame.width*startY) + startX;
	for(int i = blockOffset + offset; i < blockEnd; 
	    i += frame.width * stride) {
	    for(int j = i; j < i + blockWidth; j += stride) {
		out[j] = u.decodeSint(qf,qo);
	    }
	}
    }
    
    public void intraDCPredict(short out[]) {
	int predict = 0;
	for(int i = offset; i < out.length; i += frame.width * stride) {
	    for(int j = i; j < i + frame.width; j += stride) {
		if(j > i && i > 0) {
		    predict = Util.mean(out[j - stride], 
					out[j - frame.width*stride],
					out[j - frame.width*stride - stride]);
		} else if(j > i && i == 0) {
		    predict = out[j-stride];
		} else if(j == i && i > 0) {
		    predict = out[j-stride*frame.width];
		} else {
		    predict = 0;
		}
		out[j] += predict;
	    }
	}
    }

    private int quantFactor(int qi) {	    
	int base = (1 << (qi >>> 2));
	switch(qi & 0x3) {
	case 0:
	    return base << 2;
	case 1:
	    return (503829 * base + 52958)/105917;
	case 2:
	    return (665857 * base + 58854)/117708;
	case 3:
	default:
	    return (440253 * base + 32722)/65444;
	}
    }

    private int quantOffset(int qi) {
	if(qi == 0) {
	    return 0;
	} else {
	    if(par.num_refs == 0) {
		if(qi == 1) {
		    return 2;
		} else {
		    return (quantFactor(qi) + 1) / 2;
		}
	    } else {
		return (quantFactor(qi) * 3 + 4) / 8;
	    }
	}
    }
}


class Parameters {
    /* all that matters for now is wavelet depth */
    public int transformDepth = 4, wavelet_index;
    public int codeblock_mode_index = 0;
    public boolean no_ac, is_ref, is_lowdelay;
    public int[] horiz_codeblocks = new int[7],
	vert_codeblocks = new int[7];
    public int num_refs;
    public Dimension iwtLumaSize, iwtChromaSize;

    public Parameters(int c) {
	no_ac = !((c & 0x48) == 0x8);
	num_refs = (c & 0x3);
	is_ref = (c & 0x0c) == 0x0c;
	is_lowdelay = ((c & 0x88) == 0x88);
    }

    public void calculateIwtSizes(VideoFormat format) {
	int size[] = {0,0};
	format.getPictureLumaSize(size);
	iwtLumaSize = new Dimension(Util.roundUpPow2(size[0], transformDepth),
				    Util.roundUpPow2(size[1], transformDepth));
	format.getPictureChromaSize(size);
	iwtChromaSize = new Dimension(Util.roundUpPow2(size[0], transformDepth),
				      Util.roundUpPow2(size[1], transformDepth));
    }

    public String toString() {
	StringBuilder sb = new StringBuilder();
	sb.append("\nParameters:\n");
	sb.append(String.format("Transform depth: %d\n", transformDepth));
	sb.append(String.format("Using ac: %c\n", (no_ac ? 'n' : 'y')));
	sb.append(String.format("Is ref: %c\n", (is_ref ? 'y' : 'n')));
	return sb.toString();
    }
}    


public class Picture {
    private Buffer buf;
    private Wavelet wav;
    private Decoder dec;
    private VideoFormat format;
    private Parameters par;
    private int code;
    private Picture[] refs = {null,null};
    private boolean zero_residual = false;
    private SubBand[][] coeffs;
    private short[][] frame;
    private BufferedImage img;
    public Decoder.Status status = Decoder.Status.OK;
    public Exception error = null;
    public final int num;


    /** Picture:
     * @c: picture parse code
     * @n: picture number
     * @b: payload buffer
     * @d: decoder of the picture 
     *
     * The b buffer should only point to the payload data section of 
     * the picture (not the header). The only methods that would ever need 
     * to be called are parse(), decode(), and getImage(). However,
     * one should check wether the error variable is set before and after
     * calling a method. One should not call them in any other order than that
     * just specified. Each can be called without arguments and should not be 
     * called twice. */

    public Picture(int c, int n, Buffer b, Decoder d) {
	num = n;
	code = c;
	buf = b;
	dec = d;
	format = d.getVideoFormat();
	par = new Parameters(c);
	coeffs = new SubBand[3][19];
    }

    public void parse() {
	try {
	    Unpack u = new Unpack(buf);
	    parseHeader(u);
	    if(par.num_refs > 0) {
		u.align();
		parsePredictionParameters(u);
		u.align();
		parseBlockData(u);
	    }
	    u.align();
	    if(par.num_refs > 0) {
		zero_residual = u.decodeBool();
	    }
	    if(!zero_residual) {
		parseTransformParameters(u);
		u.align();
		if(par.is_lowdelay) {
		    parseLowDelayTransformData(u);
		} else {
		    par.calculateIwtSizes(format);
		    parseTransformData(u);
		}
	    }
	} catch(Exception e) {
	    error = e;
	    status = Decoder.Status.ERROR;
	}
	buf = null;
    }

    private void parseHeader(Unpack u) throws Exception {
	u.align();
	if(par.num_refs > 0) {
	    refs[0] = dec.refs.get(num + u.decodeSint());
	}
	if(par.num_refs > 1) {
	    refs[1] = dec.refs.get(num + u.decodeSint());
	}
	if(par.is_ref) {
	    int r = u.decodeSint();
	    if(r != 0) {
		dec.refs.remove(r + num);
	    }
	    dec.refs.add(this);
	}
    }
    
    private void parsePredictionParameters(Unpack u) throws Exception {
	System.err.println("parsePredictionParamters()");
    }

    private void parseBlockData(Unpack u) throws Exception {
	System.err.println("parseBlockData()");
    }

    private void parseTransformParameters(Unpack u) throws Exception {
	par.wavelet_index = u.decodeUint();
	par.transformDepth = u.decodeUint();
	if(!par.is_lowdelay) {
	    if(u.decodeBool()) {
		for(int i = 0; i < par.transformDepth + 1; i++) {
		    par.horiz_codeblocks[i] = u.decodeUint();
		    par.vert_codeblocks[i] = u.decodeUint();
		}
		par.codeblock_mode_index = u.decodeUint();
	    } else {
		for(int i = 0; i < par.transformDepth + 1; i++) {
		    par.horiz_codeblocks[i] = 1;
		    par.vert_codeblocks[i] = 1;
		}
	    }
	} else {
	    throw new Exception("Unhandled stream type");
	}
    }

    private void parseTransformData(Unpack u) throws Exception {
	for(int c = 0; c < 3; c++) {
	    for(int i = 0; i < 1+3*par.transformDepth; i++) {
		u.align();
		int l = u.decodeUint();
		if( l != 0) {
		    int q = u.decodeUint();
		    Buffer b = u.getSubBuffer(l);
		    coeffs[c][i] = new SubBand(b,q,par);
		    coeffs[c][i].calculateSizes(i, c == 0);
		} else {
		    coeffs[c][i] = new SubBand(null,0,par);
		    coeffs[c][i].calculateSizes(i, c == 0);
		}
	    }
	}
    }


    private void parseLowDelayTransformData(Unpack u) {
	System.err.println("parseLowDelayTransformData()");
    }

    public void decode() {
	if(!zero_residual) {
	    decodeWaveletTransform();
	}
	createImage();
    }

    
    private void decodeWaveletTransform() {
	initializeTransformFrames();
	for(int c = 0; c < 3; c++) {
	    Dimension dim = (c == 0) ? par.iwtLumaSize : par.iwtChromaSize;
	    coeffs[c][0].decodeCoeffs(frame[c]);
	    coeffs[c][0].intraDCPredict(frame[c]);
	    for(int i = 0; i < par.transformDepth; i++) {
		coeffs[c][3*i+1].decodeCoeffs(frame[c]);
		coeffs[c][3*i+2].decodeCoeffs(frame[c]);
		coeffs[c][3*i+3].decodeCoeffs(frame[c]);
	    } 
	    Wavelet.inverse(frame[c], dim.width, par.transformDepth);  
	}
    }

    private void initializeTransformFrames() {
	Dimension lum = par.iwtLumaSize;
	Dimension chrom = par.iwtChromaSize;
	frame = new short[3][];
	frame[0] = new short[lum.width * lum.height];
	frame[1] = new short[chrom.width * chrom.height];
	frame[2] = new short[chrom.width * chrom.height];
    }

    private void decodeYUV(int pixels[]) {
        Dimension lum = par.iwtLumaSize;
	Dimension chrom = par.iwtChromaSize;
	ColourSpace col = format.colour;
	short y,u,v;
	int xFac = (lum.width > chrom.width ? 2 : 1);
	int yFac = (lum.height > chrom.height ? 2 : 1);
        for(int i = 0; i < format.height; i++) {
            for(int j = 0; j < format.width; j++) {
		y = (short)(frame[0][j + i*lum.width]+128);
		u = v = 0;
                pixels[j + i*format.width] = col.convert(y,u,v);
            }
        }
    }
    
    private void createImage() {
	img = new BufferedImage(format.width , format.height , 
				BufferedImage.TYPE_INT_RGB);
	int pixels[] = new int[format.width * format.height];
	if(!zero_residual) {
	    decodeYUV(pixels);
	}
	img.setRGB(0,0, format.width, format.height, pixels, 0, format.width);
    }

    public Image getImage() {
	if(img == null) {
	    createImage();
	}
	return img;
    }
    
    public String toString() {
	StringBuilder b = new StringBuilder();	   
	b.append(String.format("Picture number: %d with code %02X",
			       num, code));
	b.append(par.toString());
	if(status == Decoder.Status.OK) {
	    if(par.num_refs > 0) {
		b.append(String.format("\nHas %d reference(s)",
				       par.num_refs));
	    }
	    if(par.is_ref) {
		b.append("\nIs a reference");
	    }
	    for(int i = 0; i < par.num_refs; i++) {
		b.append(String.format("\n\treference[%d]: %d", 
				       i, refs[i].num));
	    }
	} else {
	    b.append(String.format("Picture ERROR: %s", error.toString()));
	}

	return b.toString();
    }

}
